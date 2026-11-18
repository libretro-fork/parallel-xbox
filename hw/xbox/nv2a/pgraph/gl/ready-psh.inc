/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Render/context-thread ownership only. Native fragments live in two reserved
 * slots of the existing module pool until all binding/pipeline readers retire.
 * Texture aliases borrow existing GL names, never texture pixels or CPU views. */
static ShaderModuleCacheEntry *ready_psh_find(PGRAPHGLState *r,
                                             const ShaderModuleCacheKey *key)
{
    uint32_t controls[8];
    if (!r->ready_psh.ready || key->kind != GL_FRAGMENT_SHADER ||
        key->psh.glsl_opts.vulkan ||
        !pgraph_glsl_psh_ready_controls(&key->psh.state, controls)) return NULL;
    return &r->ready_psh.stages[!!key->psh.state.smooth_shading];
}

/* The heuristic queues legacy family keys before the draw applies its
 * helper-liveness projection. Adopt only when that same draw projection has
 * a fully linked ready fragment; restore the family's exact key before the
 * notification hashes it. The caller owns this dequeued metadata record. */
static bool ready_psh_adopt_gate(PGRAPHGLState *r, PshState *state)
{
    if (!r->ready_psh.ready) return false;
    unsigned depth_format = state->depth_format;
    uint32_t controls[8];
    state->depth_format |= DEPTH_FORMAT_HELPER_LIVENESS;
    bool covered = pgraph_glsl_psh_ready_controls(state, controls);
    state->depth_format = depth_format;
    if (covered) pgraph_glsl_psh_dynamic_ready(state);
    return covered;
}

static void ready_psh_invalidate(PGRAPHGLState *r, const ShaderBinding *binding)
{
    if (r->ready_psh.uploaded_binding == binding)
        r->ready_psh.uploaded_binding = NULL;
}

static void ready_psh_finalize(PGRAPHGLState *r)
{
    r->ready_psh.ready = false;
    r->ready_psh.uploaded_binding = NULL;
    /* Shader finalization follows binding/pipeline flush, precedes textures.
     * Clear every alias before those texture owners/pools can be destroyed. */
    if (r->ready_psh.aliases_bound) {
        GLint active;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        for (unsigned i = 0; i < 8; i++) {
            glActiveTexture(GL_TEXTURE4 + i);
            glBindTexture(i < 4 ? GL_TEXTURE_3D : GL_TEXTURE_CUBE_MAP, 0);
        }
        glActiveTexture(active);
    }
    if (r->ready_psh.stages) {
        for (unsigned i = 0; i < 2; i++) {
            ShaderModuleCacheEntry *stage = &r->ready_psh.stages[i];
            if (stage->gl_stage_program) delete_program(r, stage->gl_stage_program);
            memset(stage, 0, sizeof(*stage));
        }
    }
    r->ready_psh.stages = NULL;
    r->ready_psh.aliases_valid = 0;
    r->ready_psh.aliases_bound = false;
}

static void ready_psh_init(PGRAPHGLState *r)
{
    if (!r->ready_psh.stages) return;
    assert(!r->ready_psh.ready);
    ShaderModuleCacheEntry *reserved = r->ready_psh.stages;
    bool clean_entry = false;
    unsigned error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "Prepared fragment initialization declined: prior GL error 0x%x\n", error);
        goto unavailable;
    }
    clean_entry = true;

    GLint units;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    if (units < 12) goto unavailable;
    memset(r->ready_psh.controls_valid, 0, sizeof(r->ready_psh.controls_valid));
    r->ready_psh.aliases_valid = 0;
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    r->ready_psh.covered_binds = r->ready_psh.control_uploads = r->ready_psh.alias_binds = 0;
#endif
    for (unsigned i = 0; i < 2; i++) {
        PshState state = {.smooth_shading=i, .combiner_dynamic=true,
                         .depth_format=DEPTH_FORMAT_HELPER_LIVENESS};
        MString *source = pgraph_glsl_gen_psh_ready(&state,
                                      (GenPshGlslOptions){.locations=true});
        if (!source) goto unavailable;
        ShaderModuleCacheEntry *stage = &reserved[i];
        stage->key.kind = GL_FRAGMENT_SHADER;
        stage->gl_stage_program = pgraph_gl_separable_program(GL_FRAGMENT_SHADER,
                                      mstring_get_str(source), &stage->gl_shader);
        mstring_unref(source);
        glDeleteShader(stage->gl_shader);
        if (!stage->gl_stage_program) goto unavailable;
    }
    for (unsigned i = 0; i < 2; i++) {
        ShaderModuleCacheEntry *stage = &reserved[i];
        GLint linked = GL_FALSE;
        glGetProgramiv(stage->gl_stage_program, GL_LINK_STATUS, &linked);
        if (!linked) goto unavailable;
        glDetachShader(stage->gl_stage_program, stage->gl_shader);
        stage->gl_shader = 0;
        module_resolve_uniform_locs(stage);
        r->ready_psh.control_locs[i] = glGetUniformLocation(stage->gl_stage_program, "fsControl[0]");
        if (r->ready_psh.control_locs[i] < 0) goto unavailable;
        /* Typed sampler namespaces never alias one unit across sampler types.
         * Names/links are resolved once here, before any guest draw. */
        for (unsigned unit = 0; unit < 4; unit++) {
            static const char *const names[3][4] = {
                {"fsTex2D0","fsTex2D1","fsTex2D2","fsTex2D3"},
                {"fsTex3D0","fsTex3D1","fsTex3D2","fsTex3D3"},
                {"fsTexCube0","fsTexCube1","fsTexCube2","fsTexCube3"},
            };
            for (unsigned kind = 0; kind < 3; kind++) {
                GLint loc = glGetUniformLocation(stage->gl_stage_program, names[kind][unit]);
                if (loc < 0) goto unavailable;
                glProgramUniform1i(stage->gl_stage_program, loc, kind * 4 + unit);
            }
        }
    }
    error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "Prepared fragment initialization failed: GL error 0x%x\n", error);
        goto unavailable;
    }
    r->ready_psh.ready = true; /* Complete native links and bindings, not modules alone. */
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    fprintf(stderr, "READY_PSH initialized 2 native fragments from existing module slots\n");
#endif
    return;
unavailable:
    /* Only errors from this optional initialization, never incoming errors.
     * Bound cleanup even if a broken/lost context keeps reporting a flag. */
    if (clean_entry) for (unsigned i = 0; i < 8; i++) {
        error = glGetError();
        if (error == GL_NO_ERROR) break;
        fprintf(stderr, "Prepared fragment initialization failed: GL error 0x%x\n", error);
    }
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    fprintf(stderr, "READY_PSH unavailable; retaining exact shader path\n");
#endif
    ready_psh_finalize(r);
    for (unsigned i = 0; i < 2; i++)
        lru_add_free(&r->shader_module_cache, &reserved[i].node);
}

static void ready_psh_aliases(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;
    bool changed = false;
    GLint active = GL_TEXTURE0;
    for (unsigned i = 0; i < 8; i++) {
        unsigned unit = i & 3;
        GLenum target = i < 4 ? GL_TEXTURE_3D : GL_TEXTURE_CUBE_MAP;
        TextureBinding *binding = r->texture_binding[unit];
        GLuint name = binding && pgraph_is_texture_enabled(pg, unit) &&
                      binding->gl_target == target ? binding->gl_texture : 0;
        if ((r->ready_psh.aliases_valid & (1u << i)) &&
            r->ready_psh.aliases[i] == name) continue;
        if (!changed) {
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            changed = true;
        }
        glActiveTexture(GL_TEXTURE4 + i);
        glBindTexture(target, name);
        r->ready_psh.aliases_bound = true;
        r->ready_psh.aliases[i] = name;
        r->ready_psh.aliases_valid |= 1u << i;
#ifdef XEMU_READY_VSH_DIAGNOSTICS
        r->ready_psh.alias_binds++;
#endif
    }
    if (changed) glActiveTexture(active); /* Helpers keep their active-unit contract. */
}

static void ready_psh_upload(PGRAPHState *pg, const ShaderBinding *binding)
{
    PGRAPHGLState *r = pg->gl_renderer_state;
    if (!r->ready_psh.ready || !binding->ready_psh) return;
    /* Texture contents/names may change with no shader-register change. */
    ready_psh_aliases(pg);
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    r->ready_psh.covered_binds++;
#endif
    if (r->ready_psh.uploaded_binding == binding) return;
    unsigned variant = !!binding->state.psh.smooth_shading;
    assert(binding->gl_prog_fs == r->ready_psh.stages[variant].gl_stage_program);
    uint32_t controls[8];
    bool covered = pgraph_glsl_psh_ready_controls(&binding->state.psh, controls);
    assert(covered);
    if (!r->ready_psh.controls_valid[variant] ||
        memcmp(r->ready_psh.controls[variant], controls, sizeof(controls))) {
        glProgramUniform4uiv(binding->gl_prog_fs, r->ready_psh.control_locs[variant], 2, controls);
        memcpy(r->ready_psh.controls[variant], controls, sizeof(controls));
        r->ready_psh.controls_valid[variant] = true;
#ifdef XEMU_READY_VSH_DIAGNOSTICS
        r->ready_psh.control_uploads++;
#endif
    }
    r->ready_psh.uploaded_binding = binding;
}

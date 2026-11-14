/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Finite native GS owners consume reserved slots of the existing module pool,
 * never additional logical cache capacity. Render/context-thread only. */
static ShaderModuleCacheEntry *ready_geom_find(PGRAPHGLState *r,
                                              const ShaderModuleCacheKey *key)
{
    if (!r->ready_geom.ready || key->kind != GL_GEOMETRY_SHADER ||
        key->geom.glsl_opts.vulkan)
        return NULL;
    int variant = pgraph_glsl_geom_dynamic_variant(&key->geom.state,
                                                   key->geom.glsl_opts);
    return variant < 0 ? NULL : &r->ready_geom.stages[variant];
}

static void ready_geom_invalidate(PGRAPHGLState *r, const ShaderBinding *binding)
{
    if (r->ready_geom.uploaded_binding == binding)
        r->ready_geom.uploaded_binding = NULL;
}

static void ready_geom_finalize(PGRAPHGLState *r)
{
    r->ready_geom.ready = false;
    r->ready_geom.uploaded_binding = NULL;
    if (!r->ready_geom.stages) return;
    for (unsigned i = 0; i < GEOM_DYNAMIC_VARIANTS; i++) {
        ShaderModuleCacheEntry *stage = &r->ready_geom.stages[i];
        if (stage->gl_stage_program) delete_program(r, stage->gl_stage_program);
        memset(stage, 0, sizeof(*stage));
    }
    r->ready_geom.stages = NULL;
}

static void ready_geom_init(PGRAPHGLState *r)
{
    if (!r->ready_geom.stages) return; /* No SSO: nothing was reserved. */
    assert(!r->ready_geom.ready);
    memset(r->ready_geom.controls_valid, 0, sizeof(r->ready_geom.controls_valid));
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    r->ready_geom.covered_binds = r->ready_geom.control_uploads = 0;
#endif
    for (unsigned i = 0; i < GEOM_DYNAMIC_VARIANTS; i++) {
        GeomState state;
        GenGeomGlslOptions opts;
        pgraph_glsl_geom_dynamic_state(i, &state, &opts);
        MString *source = pgraph_glsl_gen_geom_dynamic(&state, opts);
        ShaderModuleCacheEntry *stage = &r->ready_geom.stages[i];
        stage->key.kind = GL_GEOMETRY_SHADER;
        GLuint shader;
        stage->gl_stage_program = pgraph_gl_separable_program(
            GL_GEOMETRY_SHADER, mstring_get_str(source), &shader);
        mstring_unref(source);
        stage->gl_shader = shader;
        glDeleteShader(shader);
    }
    /* Submit the finite set first. Driver compiler parallelism is unchanged;
     * no guest draw can reach these owners until every link has completed. */
    for (unsigned i = 0; i < GEOM_DYNAMIC_VARIANTS; i++) {
        ShaderModuleCacheEntry *stage = &r->ready_geom.stages[i];
        GLint linked = GL_FALSE;
        glGetProgramiv(stage->gl_stage_program, GL_LINK_STATUS, &linked);
        if (!linked) {
            fprintf(stderr, "Experimental ready geometry stage unavailable\n");
            ShaderModuleCacheEntry *reserved = r->ready_geom.stages;
            ready_geom_finalize(r);
            /* Startup failed before publication. Restore every reserved slot
             * to the normal LRU, with its original capacity and exact path. */
            for (unsigned j = 0; j < GEOM_DYNAMIC_VARIANTS; j++)
                lru_add_free(&r->shader_module_cache, &reserved[j].node);
            return;
        }
        glDetachShader(stage->gl_stage_program, stage->gl_shader);
        stage->gl_shader = 0; /* Release finished compiler objects before gameplay. */
        module_resolve_uniform_locs(stage);
        /* Some smooth line programs consume no controls; -1 is valid there. */
        r->ready_geom.control_locs[i] =
            glGetUniformLocation(stage->gl_stage_program, "gsControl");
    }
    r->ready_geom.ready = true; /* All native stages linked before guest work. */
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    fprintf(stderr, "READY_GEOM initialized %u native stages from existing module slots\n",
            GEOM_DYNAMIC_VARIANTS);
#endif
}

static void ready_geom_upload(PGRAPHGLState *r, const ShaderBinding *binding)
{
    if (!r->ready_geom.ready || binding->ready_geom_variant < 0) return;
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    r->ready_geom.covered_binds++;
#endif
    if (r->ready_geom.uploaded_binding == binding) return;
    unsigned variant = binding->ready_geom_variant;
    assert(variant < GEOM_DYNAMIC_VARIANTS && binding->gl_prog_gs ==
           r->ready_geom.stages[variant].gl_stage_program);
    if (r->ready_geom.control_locs[variant] < 0) {
        r->ready_geom.uploaded_binding = binding;
        return; /* This linked stage does not consume any packed controls. */
    }
    int controls[4];
    pgraph_glsl_geom_dynamic_controls(&binding->state.geom,
        pgraph_glsl_ui_clamp_stage(&binding->state.psh), controls);
    if (!r->ready_geom.controls_valid[variant] ||
        memcmp(r->ready_geom.controls[variant], controls, sizeof(controls))) {
        /* Bounded 16-byte packing of guest controls, not a recipe snapshot.
         * GL consumes the stack lend before return; each native stage owns
         * its uniform shadow until that context generation is retired. */
        glProgramUniform4iv(binding->gl_prog_gs,
                           r->ready_geom.control_locs[variant], 1, controls);
        memcpy(r->ready_geom.controls[variant], controls, sizeof(controls));
        r->ready_geom.controls_valid[variant] = true;
#ifdef XEMU_READY_VSH_DIAGNOSTICS
        r->ready_geom.control_uploads++;
#endif
    }
    r->ready_geom.uploaded_binding = binding;
}

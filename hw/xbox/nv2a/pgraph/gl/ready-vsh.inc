/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Experimental native programmable cover. This is a stage-readiness probe,
 * not complete graphics pipeline coverage. Ordinary cache identities and disk
 * formats remain unchanged. Enable only in a separately rebuilt test core. */
static ShaderModuleCacheEntry *ready_vsh_find(PGRAPHGLState *r,
                                             const ShaderModuleCacheKey *key)
{
    const VshState *s = &key->vsh.state;
    if (!r->ready_vsh.ready || key->kind != GL_VERTEX_SHADER ||
        s->is_fixed_function || s->compressed_attrs || s->uniform_attrs ||
        s->swizzle_attrs || s->const_attrs_dynamic ||
        s->programmable.program_length <= 0 ||
        s->programmable.program_length > NV2A_MAX_TRANSFORM_PROGRAM_LENGTH)
        return NULL;
    /* Reuse the allocation-free validated instruction domain, including FINAL,
     * live operand bounds and unsupported writes. This does not emit SPIR-V;
     * GL's numerical environment was checked by the independent GPU corpus. */
    if (!pgraph_spv_vsh_supported(s, (SpvVshOptions){ .preserve_float32 = true }))
        return NULL;
    unsigned lane = (s->smooth_shading ? 2u : 0u) |
                    (key->vsh.glsl_opts.prefix_outputs ? 1u : 0u);
    return &r->ready_vsh.stage[lane];
}

static void ready_vsh_invalidate(PGRAPHGLState *r, const ShaderBinding *binding)
{
    if (r->ready_vsh.uploaded_binding == binding)
        r->ready_vsh.uploaded_binding = NULL;
}

static void ready_vsh_finalize(PGRAPHGLState *r)
{
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    if (r->ready_vsh.ready)
        fprintf(stderr, "READY_VSH stages=4 covered=%"PRIu64" uploads=%"PRIu64"\n",
                r->ready_vsh.covered_binds, r->ready_vsh.code_uploads);
#endif
    r->ready_vsh.ready = false;
    r->ready_vsh.uploaded_binding = NULL;
    for (unsigned i = 0; i < ARRAY_SIZE(r->ready_vsh.stage); i++) {
        ShaderModuleCacheEntry *stage = &r->ready_vsh.stage[i];
        if (stage->gl_stage_program) delete_program(r, stage->gl_stage_program);
        memset(stage, 0, sizeof(*stage));
    }
    if (r->ready_vsh.code_buffer)
        glDeleteBuffers(1, &r->ready_vsh.code_buffer);
    r->ready_vsh.code_buffer = 0;
}

static void ready_vsh_init(PGRAPHGLState *r)
{
    if (!gl_sso_enabled()) return;
    assert(!r->ready_vsh.ready && !r->ready_vsh.code_buffer);
    for (unsigned i = 0; i < ARRAY_SIZE(r->ready_vsh.stage); i++) {
        VshState state = { .smooth_shading = !!(i & 2) };
        GenVshGlslOptions options = { .locations = true,
                                     .prefix_outputs = !!(i & 1) };
        MString *source = pgraph_glsl_gen_vsh_interpreter_dynamic(&state, options);
        ShaderModuleCacheEntry *stage = &r->ready_vsh.stage[i];
        stage->key.kind = GL_VERTEX_SHADER;
        GLuint shader;
        stage->gl_stage_program = pgraph_gl_separable_program(
            GL_VERTEX_SHADER, mstring_get_str(source), &shader);
        mstring_unref(source);
        glDeleteShader(shader); /* Program retains it until its last consumer. */
        GLint linked = GL_FALSE;
        glGetProgramiv(stage->gl_stage_program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[2048];
            glGetProgramInfoLog(stage->gl_stage_program, sizeof(log), NULL, log);
            fprintf(stderr, "Experimental ready vertex stage unavailable: %s\n", log);
            ready_vsh_finalize(r);
            return; /* Keep the existing correct specialized path. */
        }
        GLuint block = glGetUniformBlockIndex(stage->gl_stage_program, "XvsCode");
        assert(block != GL_INVALID_INDEX);
        glUniformBlockBinding(stage->gl_stage_program, block, 0);
        module_resolve_uniform_locs(stage);
    }
    GLint previous;
    glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &previous);
    glGenBuffers(1, &r->ready_vsh.code_buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, r->ready_vsh.code_buffer);
    glBufferData(GL_UNIFORM_BUFFER, VSH_INTERPRETER_BUFFER_SIZE, NULL,
                 GL_STREAM_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, previous);
    r->ready_vsh.uploaded_binding = NULL;
    r->ready_vsh.ready = true; /* Owner-only publication, before guest execution. */
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    fprintf(stderr, "READY_VSH initialized 4 native stages before guest execution\n");
#endif
}

static void ready_vsh_upload(PGRAPHGLState *r, const ShaderBinding *binding)
{
    if (!r->ready_vsh.ready) return;
    bool covered = false;
    for (unsigned i = 0; i < ARRAY_SIZE(r->ready_vsh.stage); i++)
        covered |= binding->gl_prog_vs == r->ready_vsh.stage[i].gl_stage_program;
    if (!covered) return;
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    r->ready_vsh.covered_binds++;
#endif
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->ready_vsh.code_buffer);
    if (r->ready_vsh.uploaded_binding == binding) return;
    const ShaderBinding *resident = r->ready_vsh.uploaded_binding;
    const VshState *state = &binding->state.vsh;
    uint32_t flags = pgraph_glsl_vsh_interpreter_flags(state);
    bool code_changed = !resident || memcmp(
        resident->state.vsh.programmable.program_data,
        state->programmable.program_data, VSH_INTERPRETER_FLAGS_OFFSET) != 0;
    bool flags_changed = !resident || flags !=
        pgraph_glsl_vsh_interpreter_flags(&resident->state.vsh);
    /* The current immutable cache entry witnesses the resident bytes. Change
     * the lend to this binding even on a byte-identical hit; eviction cancels
     * it before reuse. No duplicate microcode snapshot is retained. */
    if (code_changed || flags_changed)
        glBindBuffer(GL_UNIFORM_BUFFER, r->ready_vsh.code_buffer);
    if (code_changed) {
#ifdef XEMU_READY_VSH_DIAGNOSTICS
        r->ready_vsh.code_uploads++;
#endif
        /* GL snapshots SubData before return and retains its buffer through
         * queued consumers. Driver copies/stalls/allocations remain open. */
        glBufferSubData(GL_UNIFORM_BUFFER, 0, VSH_INTERPRETER_FLAGS_OFFSET,
                       state->programmable.program_data);
    }
    if (flags_changed)
        glBufferSubData(GL_UNIFORM_BUFFER, VSH_INTERPRETER_FLAGS_OFFSET,
                       sizeof(flags), &flags);
    r->ready_vsh.uploaded_binding = binding;
}

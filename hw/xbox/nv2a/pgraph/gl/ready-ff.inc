/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The fixed-function families use ordinary shader math and their
 * existing uniform ABI. Only fully validated startup tuples publish coverage.
 * Every owner is borrowed from an existing cache pool on the context thread. */
static ShaderModuleCacheEntry *ready_ff_find(PGRAPHGLState *r,
                                            const ShaderModuleCacheKey *key)
{
#ifdef XEMU_EXPERIMENTAL_READY_FF
    if (!r->ready_ff.ready || key->kind != GL_VERTEX_SHADER ||
        key->vsh.glsl_opts.vulkan) return NULL;
    int index = ready_ff_state_index(&key->vsh.state,
                                     key->vsh.glsl_opts.prefix_outputs);
    return index < 0 ? NULL : &r->ready_ff.stages[index];
#else
    (void)r; (void)key;
    return NULL;
#endif
}

#ifdef XEMU_EXPERIMENTAL_READY_FF
static void ready_ff_pipelines_finalize(PGRAPHGLState *r)
{
    r->ready_ff.ready = false;
    if (!r->ready_ff.pipelines) return;
    for (unsigned i = 0; i < GL_READY_FF_PIPELINE_COUNT; i++) {
        pipeline_owner_release(r, &r->ready_ff.pipelines[i]);
        memset(&r->ready_ff.pipelines[i], 0,
               sizeof(*r->ready_ff.pipelines));
    }
    r->ready_ff.pipelines = NULL;
}

static void ready_ff_stages_finalize(PGRAPHGLState *r)
{
    r->ready_ff.ready = false;
    if (!r->ready_ff.stages) return;
    for (unsigned i = 0; i < GL_READY_FF_STAGE_COUNT; i++) {
        ShaderModuleCacheEntry *stage = &r->ready_ff.stages[i];
        if (stage->gl_stage_program) delete_program(r, stage->gl_stage_program);
        memset(stage, 0, sizeof(*stage));
    }
    r->ready_ff.stages = NULL;
}

static void ready_ff_init(PGRAPHGLState *r)
{
    ShaderModuleCacheEntry *stages = r->ready_ff.stages;
    ShaderBinding *pipelines = r->ready_ff.pipelines;
    if (!stages || !pipelines) return; /* No SSO: no reservations. */
    assert(!r->ready_ff.ready);
    if (!r->ready_geom.ready || !r->ready_psh.ready) goto unavailable;

    for (unsigned i = 0; i < GL_READY_FF_STAGE_COUNT; i++) {
        ShaderModuleCacheEntry *stage = &stages[i];
        stage->key.kind = GL_VERTEX_SHADER;
        ready_ff_state_init(&stage->key.vsh.state, i);
        stage->key.vsh.glsl_opts.locations = true;
        stage->key.vsh.glsl_opts.prefix_outputs = i & 1u;
        MString *source = pgraph_glsl_gen_vsh(&stage->key.vsh.state,
                                            stage->key.vsh.glsl_opts);
        stage->gl_stage_program = pgraph_gl_separable_program(GL_VERTEX_SHADER,
            mstring_get_str(source), &stage->gl_shader);
        mstring_unref(source);
        glDeleteShader(stage->gl_shader); /* Executable retains its input. */
    }
    for (unsigned i = 0; i < GL_READY_FF_STAGE_COUNT; i++) {
        ShaderModuleCacheEntry *stage = &stages[i];
        GLint linked = GL_FALSE;
        if (stage->gl_stage_program)
            glGetProgramiv(stage->gl_stage_program, GL_LINK_STATUS, &linked);
        if (!linked) goto unavailable;
        glDetachShader(stage->gl_stage_program, stage->gl_shader);
        stage->gl_shader = 0;
        module_resolve_uniform_locs(stage);
    }
    /* Each family's fourteen skin/point groups have sixteen geometry and two
     * direct tuples. Interpolation selects the already-linked stage. */
    for (unsigned i = 0; i < GL_READY_FF_PIPELINE_COUNT; i++) {
        unsigned tuple = i % 18u, group = i / 18u;
        bool geometry = tuple < 16u;
        unsigned smooth = geometry ? tuple & 1u : tuple - 16u;
        ShaderBinding *anchor = &pipelines[i];
        anchor->gl_prog_vs = stages[group * 4u + smooth * 2u + geometry].gl_stage_program;
        anchor->gl_prog_gs = geometry ? r->ready_geom.stages[tuple].gl_stage_program : 0;
        anchor->gl_prog_fs = r->ready_psh.stages[smooth].gl_stage_program;
        glGenProgramPipelines(1, &anchor->gl_pipeline);
        if (!anchor->gl_pipeline) goto unavailable;
        glUseProgramStages(anchor->gl_pipeline, GL_VERTEX_SHADER_BIT, anchor->gl_prog_vs);
        if (geometry)
            glUseProgramStages(anchor->gl_pipeline, GL_GEOMETRY_SHADER_BIT, anchor->gl_prog_gs);
        glUseProgramStages(anchor->gl_pipeline, GL_FRAGMENT_SHADER_BIT, anchor->gl_prog_fs);
        glValidateProgramPipeline(anchor->gl_pipeline);
        GLint valid = GL_FALSE;
        glGetProgramPipelineiv(anchor->gl_pipeline, GL_VALIDATE_STATUS, &valid);
        if (!valid) {
            glDeleteProgramPipelines(1, &anchor->gl_pipeline);
            anchor->gl_pipeline = 0;
            goto unavailable;
        }
        pipeline_owner_publish(r, anchor);
    }
    r->ready_ff.ready = true; /* Complete native coverage, not modules alone. */
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    fprintf(stderr, "READY_FF initialized %u ordinary stages and %u validated tuples before guest execution\n",
            GL_READY_FF_STAGE_COUNT, GL_READY_FF_PIPELINE_COUNT);
#endif
    return;
unavailable:
    ready_ff_pipelines_finalize(r);
    ready_ff_stages_finalize(r);
    for (unsigned i = 0; i < GL_READY_FF_PIPELINE_COUNT; i++)
        lru_add_free(&r->shader_cache, &pipelines[i].node);
    for (unsigned i = 0; i < GL_READY_FF_STAGE_COUNT; i++)
        lru_add_free(&r->shader_module_cache, &stages[i].node);
}
#endif

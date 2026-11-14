/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Complete the finite native tuples before guest execution. Anchors borrow
 * eighteen existing binding slots and stay in the same owner index as ordinary
 * bindings; no payload, stage or resource cache is cloned. */
static void ready_pipelines_finalize(PGRAPHGLState *r)
{
    if (!r->ready_pipelines) return;
    for (unsigned i = 0; i < 18; i++) {
        pipeline_owner_release(r, &r->ready_pipelines[i]);
        memset(&r->ready_pipelines[i], 0, sizeof(*r->ready_pipelines));
    }
    r->ready_pipelines = NULL;
}

static void ready_pipelines_init(PGRAPHGLState *r)
{
    ShaderBinding *reserved = r->ready_pipelines;
    if (!reserved) return;
    if (!r->ready_vsh.ready || !r->ready_geom.ready || !r->ready_psh.ready)
        goto unavailable;
    for (unsigned i = 0; i < 18; i++) {
        bool geometry = i < 16;
        unsigned smooth = geometry ? (i & 1) : i - 16;
        ShaderBinding *anchor = &reserved[i];
        anchor->gl_prog_vs = r->ready_vsh.stage[smooth * 2 + geometry].gl_stage_program;
        anchor->gl_prog_gs = geometry ? r->ready_geom.stages[i].gl_stage_program : 0;
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
            /* Not indexed yet: release this failed native owner directly. */
            glDeleteProgramPipelines(1, &anchor->gl_pipeline);
            anchor->gl_pipeline = 0;
            goto unavailable;
        }
        pipeline_owner_publish(r, anchor);
    }
#ifdef XEMU_READY_VSH_DIAGNOSTICS
    fprintf(stderr, "READY_PIPELINES initialized 18 validated native tuples before guest execution\n");
#endif
    return;
unavailable:
    ready_pipelines_finalize(r);
    for (unsigned i = 0; i < 18; i++)
        lru_add_free(&r->shader_cache, &reserved[i].node);
}

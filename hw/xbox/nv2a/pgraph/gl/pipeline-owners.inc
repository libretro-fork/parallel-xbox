/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Render/context-thread only. A complete native pipeline belongs to a ring of
 * live immutable bindings. The hash index stores one member, not another
 * payload or resource owner. Retirement transfers that representative before
 * its cache node can be recycled. No resource capacity exceeds the binding LRU.
 * Native handles identify this context generation only, never a disk key. */
static unsigned pipeline_owner_bucket(const ShaderBinding *binding)
{
    uint32_t hash = binding->gl_prog_vs * UINT32_C(0x9e3779b1);
    hash ^= binding->gl_prog_gs * UINT32_C(0x85ebca77);
    hash ^= binding->gl_prog_fs * UINT32_C(0xc2b2ae3d);
    hash ^= hash >> 16;
    return hash % GL_PIPELINE_OWNER_BUCKETS;
}

static bool pipeline_owner_same(const ShaderBinding *a, const ShaderBinding *b)
{
    return a->gl_prog_vs == b->gl_prog_vs &&
           a->gl_prog_gs == b->gl_prog_gs &&
           a->gl_prog_fs == b->gl_prog_fs;
}

static bool pipeline_owner_acquire(PGRAPHGLState *r, ShaderBinding *binding)
{
    assert(!binding->pipeline_next && !binding->pipeline_prev &&
           !binding->pipeline_hash_next && !binding->gl_pipeline);
    unsigned bucket = pipeline_owner_bucket(binding);
    for (ShaderBinding *owner = r->pipeline_owners[bucket]; owner;
         owner = owner->pipeline_hash_next) {
        if (!pipeline_owner_same(owner, binding)) continue;
        binding->gl_pipeline = owner->gl_pipeline;
        binding->pipeline_prev = owner;
        binding->pipeline_next = owner->pipeline_next;
        owner->pipeline_next->pipeline_prev = binding;
        owner->pipeline_next = binding;
        return true;
    }
    return false;
}

static void pipeline_owner_publish(PGRAPHGLState *r, ShaderBinding *binding)
{
    assert(binding->gl_pipeline && !binding->pipeline_next &&
           !binding->pipeline_prev && !binding->pipeline_hash_next);
    unsigned bucket = pipeline_owner_bucket(binding);
    binding->pipeline_next = binding->pipeline_prev = binding;
    binding->pipeline_hash_next = r->pipeline_owners[bucket];
    r->pipeline_owners[bucket] = binding;
}

static void pipeline_owner_release(PGRAPHGLState *r, ShaderBinding *binding)
{
    if (!binding->gl_pipeline) {
        assert(!binding->pipeline_next && !binding->pipeline_prev &&
               !binding->pipeline_hash_next);
        return;
    }
    unsigned bucket = pipeline_owner_bucket(binding);
    ShaderBinding **index = &r->pipeline_owners[bucket];
    while (*index && !pipeline_owner_same(*index, binding))
        index = &(*index)->pipeline_hash_next;
    assert(*index && (*index)->gl_pipeline == binding->gl_pipeline);
    if (binding->pipeline_next == binding) {
        assert(*index == binding && binding->pipeline_prev == binding);
        *index = binding->pipeline_hash_next;
        /* GL retains queued consumers; this was the last CPU binding owner. */
        glDeleteProgramPipelines(1, &binding->gl_pipeline);
    } else {
        if (*index == binding) {
            ShaderBinding *replacement = binding->pipeline_next;
            replacement->pipeline_hash_next = binding->pipeline_hash_next;
            *index = replacement;
        }
        binding->pipeline_prev->pipeline_next = binding->pipeline_next;
        binding->pipeline_next->pipeline_prev = binding->pipeline_prev;
    }
    binding->gl_pipeline = 0;
    binding->pipeline_next = binding->pipeline_prev = NULL;
    binding->pipeline_hash_next = NULL;
}

static void pipeline_owner_finalize(PGRAPHGLState *r)
{
    /* The ordinary cache flush must retire every borrower first. */
    for (unsigned i = 0; i < GL_PIPELINE_OWNER_BUCKETS; i++)
        assert(!r->pipeline_owners[i]);
}

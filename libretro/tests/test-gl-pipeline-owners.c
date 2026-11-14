/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
/* Every native tuple collides: collision handling cannot rely on its hash. */
#define GL_PIPELINE_OWNER_BUCKETS 1
typedef unsigned GLuint;
typedef struct ShaderBinding {
    GLuint gl_pipeline, gl_prog_vs, gl_prog_gs, gl_prog_fs;
    struct ShaderBinding *pipeline_next, *pipeline_prev, *pipeline_hash_next;
} ShaderBinding;
typedef struct {
    ShaderBinding *pipeline_owners[GL_PIPELINE_OWNER_BUCKETS];
} PGRAPHGLState;
static ShaderBinding slots[512];
static bool alive[200001];
static unsigned created, deleted, names, free_count;
static GLuint free_names[512];
static void glDeleteProgramPipelines(int n, const GLuint *pipeline)
{
    assert(n == 1 && *pipeline <= names && alive[*pipeline]);
    unsigned borrowers = 0;
    for (unsigned i = 0; i < 512; i++)
        borrowers += slots[i].gl_pipeline == *pipeline;
    assert(borrowers == 1); /* Last CPU binding only. */
    alive[*pipeline] = false;
    deleted++;
    assert(free_count < 512);
    free_names[free_count++] = *pipeline;
}
/* PRODUCTION */
static void acquire(PGRAPHGLState *r, ShaderBinding *b, unsigned key)
{
    b->gl_prog_vs = 1 + (key & 3);
    b->gl_prog_fs = 10 + ((key >> 2) & 3);
    b->gl_prog_gs = (key & 16) ? 20 : 0;
    if (!pipeline_owner_acquire(r, b)) {
        created++;
        b->gl_pipeline = free_count ? free_names[--free_count] : ++names;
        assert(b->gl_pipeline < 200001 && !alive[b->gl_pipeline]);
        alive[b->gl_pipeline] = true;
        pipeline_owner_publish(r, b);
    }
}
static void validate(PGRAPHGLState *r)
{
    bool seen[512] = { false };
    unsigned groups = 0;
    for (ShaderBinding *owner = r->pipeline_owners[0]; owner;
         owner = owner->pipeline_hash_next) {
        assert(++groups <= 32);
        ShaderBinding *p = owner;
        unsigned members = 0;
        do {
            assert(p >= slots && p < slots + 512 && !seen[p - slots]);
            seen[p - slots] = true;
            assert(p->gl_pipeline && alive[p->gl_pipeline]);
            assert(p->gl_prog_vs == owner->gl_prog_vs);
            assert(p->gl_prog_fs == owner->gl_prog_fs);
            assert(p->gl_prog_gs == owner->gl_prog_gs);
            assert(p->gl_pipeline == owner->gl_pipeline);
            assert(p->pipeline_next->pipeline_prev == p);
            assert(p->pipeline_prev->pipeline_next == p);
            assert(++members <= 512);
            p = p->pipeline_next;
        } while (p != owner);
    }
    for (unsigned i = 0; i < 512; i++) {
        ShaderBinding *b = &slots[i];
        assert(seen[i] == !!b->gl_pipeline);
        for (unsigned j = 0; j < i; j++) {
            if (b->gl_pipeline && slots[j].gl_pipeline &&
                b->gl_prog_vs == slots[j].gl_prog_vs &&
                b->gl_prog_gs == slots[j].gl_prog_gs &&
                b->gl_prog_fs == slots[j].gl_prog_fs)
                assert(b->gl_pipeline == slots[j].gl_pipeline);
        }
    }
    assert(created - deleted == groups);
}
int main(void)
{
    PGRAPHGLState r = {0};
    /* Distinct GS/FS/VS handles and zero-GS absence must never alias. */
    for (unsigned i = 0; i < 32; i++) acquire(&r, &slots[i], i);
    assert(created == 32);
    for (unsigned i = 32; i < 512; i++) acquire(&r, &slots[i], i & 31);
    assert(created == 32);
    validate(&r);
    /* Representatives retire first, then their successors and arbitrary
     * members. Nodes are immediately recycled into different tuple rings. */
    for (unsigned i = 0; i < 32; i++) pipeline_owner_release(&r, &slots[i]);
    validate(&r);
    uint32_t random = 1234567;
    for (unsigned n = 0; n < 200000; n++) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        ShaderBinding *b = &slots[random % 512];
        if (b->gl_pipeline) pipeline_owner_release(&r, b);
        else acquire(&r, b, random >> 9);
        if (!(n % 1024)) validate(&r);
    }
    validate(&r);
    for (unsigned i = 0; i < 512; i++) pipeline_owner_release(&r, &slots[i]);
    pipeline_owner_finalize(&r);
    assert(created == deleted);
    /* Fresh context generation: no stale representative may survive. */
    GLuint recycled = free_names[free_count - 1];
    acquire(&r, &slots[0], 0);
    assert(slots[0].gl_pipeline == recycled);
    acquire(&r, &slots[1], 0);
    pipeline_owner_release(&r, &slots[0]);
    pipeline_owner_release(&r, &slots[1]);
    pipeline_owner_finalize(&r);
    assert(created == deleted);
    puts("PASS: 200000 colliding native-pipeline owner transitions, exact stage identity, last-borrower retirement and node/context reuse");
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <retro_timers.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

enum { GL_VERTEX_SHADER, GL_GEOMETRY_SHADER, GL_FRAGMENT_SHADER };
typedef struct { bool locations; } GenVshGlslOptions;
typedef GenVshGlslOptions GenGeomGlslOptions;
typedef GenVshGlslOptions GenPshGlslOptions;
typedef struct { unsigned kind; struct {
    GenVshGlslOptions glsl_opts;
    unsigned state;
} vsh, geom, psh; } ShaderModuleCacheKey;
typedef struct { unsigned value; } MString;
static retro_atomic_int_t stop, generated;

static bool gl_sso_enabled(void) { return true; }
static MString *generate(const unsigned *state, GenVshGlslOptions options)
{
    MString *code;
    assert(options.locations);
    retro_atomic_fetch_add_int(&generated, 1);
    if (!(*state % 17))
        return NULL;
    code = malloc(sizeof(*code));
    assert(code);
    code->value = *state ^ 0x12345678U;
    return code;
}
#define pgraph_glsl_gen_vsh generate
#define pgraph_glsl_gen_geom generate
#define pgraph_glsl_gen_psh generate
static MString *mstring_from_str(const char *empty)
{
    MString *code = calloc(1, sizeof(*code));
    assert(code && !*empty);
    return code;
}

#include "glslgen-production.h"

int main(void)
{
    unsigned submitted = 0, completed = 0, empty = 0;
    const unsigned total = 100000;
    assert(retro_eventcount_init(&gen_ready));
    /* Fill every slot before the consumer exists: reject overflow without
     * modifying a live key, while accepting duplicates in a full queue. */
    for (unsigned value = 1; value <= GEN_JOBS; value++) {
        ShaderModuleCacheKey key = {0};
        key.kind = value % 3;
        key.vsh.state = key.geom.state = key.psh.state = value;
        assert(gen_job_push(&key, !!(value & 1), !!(value & 2)));
        assert(gen_job_push(&key, !!(value & 1), !!(value & 2)));
        submitted++;
    }
    assert(!gen_jobs_have_free());
    {
        ShaderModuleCacheKey overflow = {0};
        assert(!gen_job_push(&overflow, false, false));
    }
    gen_thr = sthread_create(shader_gen_worker, NULL);
    assert(gen_thr);
    while (completed < total) {
        for (int i = 0; i < GEN_JOBS; i++) {
            if (gen_jobs[i].used &&
                retro_atomic_load_acquire_int(&gen_jobs[i].state) == GEN_COMPLETE) {
                unsigned value = gen_jobs[i].key.vsh.state;
                assert(value == gen_jobs[i].key.geom.state);
                assert(value == gen_jobs[i].key.psh.state);
                assert(gen_jobs[i].gate == !!(value & 1));
                assert(gen_jobs[i].seed == !!(value & 2));
                assert(gen_jobs[i].code->value ==
                       (value % 17 ? (value ^ 0x12345678U) : 0));
                free(gen_jobs[i].code);
                gen_jobs[i].code = NULL;
                gen_jobs[i].used = false;
                retro_atomic_store_relaxed_int(&gen_jobs[i].state, GEN_IDLE);
                completed++;
            }
        }
        if (submitted < total && gen_jobs_have_free()) {
            ShaderModuleCacheKey key = {0};
            unsigned value = ++submitted;
            key.kind = value % 3;
            key.vsh.state = key.geom.state = key.psh.state = value;
            assert(gen_job_push(&key, !!(value & 1), !!(value & 2)));
            /* A duplicate in flight must not generate a second source. */
            assert(gen_job_push(&key, !!(value & 1), !!(value & 2)));
            retro_eventcount_notify(&gen_ready);
            if (!(value % 997))
                retro_sleep(1); /* repeatedly let the worker park */
        } else {
            empty++;
            retro_sleep(0);
        }
    }
    assert(!gen_jobs_pending() && gen_jobs_have_free());
    assert((unsigned)retro_atomic_load_acquire_int(&generated) == total);
    retro_atomic_store_release_int(&stop, 1);
    retro_eventcount_notify(&gen_ready);
    sthread_join(gen_thr);
    retro_eventcount_free(&gen_ready);
    printf("GLSL generation: %u jobs, slot reuse, duplicates and parking passed (%u waits)\n",
           completed, empty);
    return 0;
}

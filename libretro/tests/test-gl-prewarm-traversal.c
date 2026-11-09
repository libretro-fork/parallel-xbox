/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct GDir { unsigned bucket, next; bool root; char name[64]; } GDir;
typedef struct ShaderModuleCacheKey { unsigned id; } ShaderModuleCacheKey;
typedef struct PGRAPHGLState {
    /* PREWARM */
} PGRAPHGLState;
static struct { struct { bool cache_shaders; } perf; } g_config;
static bool sso, root_exists;
static unsigned buckets, files, reads, opens, active_dirs, paths, peak_paths, attempts, accepted;
static unsigned seen[65536];
static bool gl_sso_enabled(void) { return sso; }
static const char *xemu_settings_get_base_path(void) { return "fixture"; }
static char *g_strdup_printf(const char *format, ...) {
    va_list ap; va_start(ap, format); char *p; assert(vasprintf(&p, format, ap) >= 0); va_end(ap);
    paths++; if (paths > peak_paths) peak_paths = paths; assert(paths <= 3); return p;
}
static void g_free(void *p) { if (p) { assert(paths); paths--; free(p); } }
static GDir *g_dir_open(const char *path, unsigned flags, void *error) {
    (void)flags; (void)error; opens++;
    bool root = !strcmp(path, "fixture/shader_modules"); unsigned bucket = 0; int n = 0;
    if (root) { if (!root_exists) return NULL; }
    else {
        assert(sscanf(path, "fixture/shader_modules/bucket%u%n", &bucket, &n) == 1 && !path[n]);
        assert(bucket < buckets); if (bucket % 3 == 1) return NULL;
    }
    GDir *d = calloc(1, sizeof(*d)); assert(d); d->root = root; d->bucket = bucket;
    active_dirs++; assert(active_dirs <= 2); return d;
}
static const char *g_dir_read_name(GDir *d) {
    assert(d); reads++;
    /* Each read invalidates the previous borrowed entry name. */
    memset(d->name, 0xa5, sizeof(d->name));
    unsigned end = d->root ? buckets : (d->bucket % 5 == 0 ? 0 : files);
    if (d->next >= end) return NULL;
    snprintf(d->name, sizeof(d->name), d->root ? "bucket%u" : "file%u", d->next++);
    return d->name;
}
static void g_dir_close(GDir *d) { assert(d && active_dirs); active_dirs--; memset(d, 0xa5, sizeof(*d)); free(d); }
static bool module_file_read_key(const char *path, ShaderModuleCacheKey *key) {
    unsigned b, f; int n = 0;
    assert(sscanf(path, "fixture/shader_modules/bucket%u/file%u%n", &b, &f, &n) == 2 && !path[n]);
    assert(b < buckets && b % 3 != 1 && b % 5 != 0 && f < files);
    key->id = b * files + f; assert(key->id < 65536); attempts++;
    assert(!seen[key->id]++); return key->id % 7 != 0; /* corrupt/stale header */
}
static void get_shader_module_for_key(PGRAPHGLState *r, const ShaderModuleCacheKey *key) {
    assert(r->prewarm.root && r->prewarm.bucket && key->id % 7 != 0); accepted++;
}
/* PRODUCTION */
static void run(unsigned nb, unsigned nf, unsigned quota, int cancel_at) {
    assert(!paths && !active_dirs); buckets = nb; files = nf;
    reads = opens = peak_paths = attempts = accepted = 0; memset(seen, 0, sizeof(seen));
    PGRAPHGLState r = { .prewarm.state = -1 };
    module_prewarm_pump(&r, 0); assert(!opens && !reads && r.prewarm.state == -1);
    unsigned ticks = 0;
    while (r.prewarm.state != 0) {
        if (cancel_at >= 0 && ticks == (unsigned)cancel_at) break;
        unsigned before = reads, before_attempts = attempts;
        module_prewarm_pump(&r, quota);
        assert(reads - before <= quota && attempts - before_attempts <= quota);
        assert(paths <= 2 && active_dirs <= 2); assert(++ticks < 100000);
    }
    if (cancel_at < 0 && root_exists && g_config.perf.cache_shaders && sso) {
        unsigned expected = 0, valid = 0;
        for (unsigned b = 0; b < buckets; b++) if (b % 3 != 1 && b % 5 != 0)
            for (unsigned f = 0; f < files; f++) { unsigned id = b * files + f; assert(seen[id] == 1); expected++; valid += id % 7 != 0; }
        assert(attempts == expected && accepted == valid && r.prewarm.count == expected);
    }
    module_prewarm_finish(&r); assert(!paths && !active_dirs && r.prewarm.state == 0);
    assert(!r.prewarm.root && !r.prewarm.bucket && !r.prewarm.base && !r.prewarm.bucket_path);
    unsigned before = reads + opens; module_prewarm_finish(&r); module_prewarm_pump(&r, quota);
    assert(before == reads + opens && !paths && !active_dirs);
}
int main(void) {
    g_config.perf.cache_shaders = sso = root_exists = true;
    unsigned cases = 0;
    const unsigned counts[] = {0, 1, 8, 64}, sizes[] = {0, 1, 17, 1024}, quotas[] = {1, 3, 8};
    for (unsigned b = 0; b < 4; b++) for (unsigned f = 0; f < 4; f++) for (unsigned q = 0; q < 3; q++) {
        run(counts[b], sizes[f], quotas[q], -1); cases++;
        for (int stop = 0; stop < 20; stop++) { run(counts[b], sizes[f], quotas[q], stop); cases++; }
    }
    root_exists = false; run(8, 17, 1, -1); assert(!reads && !paths && !active_dirs); cases++;
    root_exists = true;
    g_config.perf.cache_shaders = false; run(8, 17, 1, -1); assert(!reads && !opens); cases++;
    g_config.perf.cache_shaders = true; sso = false; run(8, 17, 1, -1); assert(!reads && !opens); cases++;
    printf("GL prewarm: %u bounded traversal/cancellation cases, borrowed names, constant owner bounds and off-path zero I/O PASS\n", cases);
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_mpsc.h>
#include <rthreads/rthreads.h>
#include <retro_timers.h>
#include "qemu/lru.h"

typedef unsigned GLenum;
typedef struct { unsigned id; } ShaderState;
typedef struct {
    LruNode node;
    ShaderState state;
    bool initialized, cached;
    void *program;
    size_t program_size;
    GLenum program_format;
    sthread_t *save_thread;
} ShaderBinding;
typedef struct {
    retro_mpsc_t shader_disk_results;
    retro_mpsc_node_t *shader_disk_pending;
    Lru shader_cache;
    sthread_t *shader_disk_thread;
    unsigned shader_cache_writeback_complete, shader_cache_writeback_pending;
} PGRAPHGLState;
typedef struct { PGRAPHGLState *gl_renderer_state; } PGRAPHState;
static struct { struct { bool cache_shaders; } perf; } g_config;
static PGRAPHGLState renderer;
static ShaderBinding bindings[128];
static _Thread_local bool owner;
static retro_atomic_int_t done;
static const char *directory, *shader_gl_vendor = "test-vendor", *shader_gl_version = "test-version";
#define container_of(p, type, field) ((type *)((char *)(p) - offsetof(type, field)))
#define g_new(type, n) ((type *)malloc(sizeof(type) * (n)))
#define g_free free
#define qemu_fopen fopen
#define qemu_unlink remove
static void qemu_mkdir(const char *path) { (void)path; }
static char *shader_get_bin_directory(uint64_t hash) { return strdup(directory); }
static char *shader_get_binary_path(const char *path, uint64_t hash)
{
    size_t size = strlen(path) + 40;
    char *name = malloc(size);
    assert(name);
    snprintf(name, size, "%s/%llu.bin", path, (unsigned long long)hash);
    return name;
}
static bool shader_write_cache_version(FILE *file)
{
    return fwrite("TEST", 4, 1, file) == 1;
}
static char *shader_get_lru_cache_path(void) { return shader_get_binary_path(directory, 0); }
static void seed_write_files(PGRAPHGLState *r) { assert(owner); }
static void pgraph_clear_and_notify(unsigned *complete, unsigned *pending)
{
    assert(owner);
    *pending = 0;
    (*complete)++;
}
#include "shader-cache-production.h"

static void initialize(Lru *lru, LruNode *node, const void *key)
{
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    assert(owner);
    binding->state = *(const ShaderState *)key;
    binding->program = NULL;
    binding->initialized = binding->cached = false;
}
static bool differs(Lru *lru, LruNode *node, const void *key)
{
    assert(owner);
    return container_of(node, ShaderBinding, node)->state.id != ((const ShaderState *)key)->id;
}
static void evict(Lru *lru, LruNode *node)
{
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    assert(owner);
    free(binding->program);
    binding->program = NULL;
}
static void publish(void *arg)
{
    for (unsigned i = 0; i < 2000; i++) {
        unsigned id = (i + (unsigned)(uintptr_t)arg) % 64;
        ShaderDiskResult *result = calloc(1, sizeof(*result));
        assert(result);
        result->state.id = id;
        result->hash = id % 4; /* exercise hash collisions */
        result->program_format = 19;
        result->program_size = sizeof(unsigned);
        result->program = malloc(sizeof(unsigned));
        assert(result->program);
        *(unsigned *)result->program = id ^ 0x12345678U;
        retro_mpsc_push(&renderer.shader_disk_results, &result->node);
    }
    retro_atomic_fetch_add_int(&done, 1);
}
int main(int argc, char **argv)
{
    sthread_t *threads[8];
    assert(argc == 2);
    directory = argv[1];
    owner = true;
    retro_mpsc_init(&renderer.shader_disk_results);
    lru_init(&renderer.shader_cache);
    renderer.shader_cache.init_node = initialize;
    renderer.shader_cache.compare_nodes = differs;
    renderer.shader_cache.post_node_evict = evict;
    for (unsigned i = 0; i < 128; i++)
        lru_add_free(&renderer.shader_cache, &bindings[i].node);
    ShaderState compiled_key = { 1 };
    ShaderBinding *compiled = container_of(lru_lookup(&renderer.shader_cache, 1, &compiled_key), ShaderBinding, node);
    compiled->initialized = true;
    for (unsigned i = 0; i < 8; i++) {
        threads[i] = sthread_create(publish, (void *)(uintptr_t)i);
        assert(threads[i]);
    }
    while (retro_atomic_load_acquire_int(&done) != 8) {
        shader_adopt_disk_results(&renderer, 3);
        retro_sleep(0);
    }
    for (unsigned i = 0; i < 8; i++) sthread_join(threads[i]);
    shader_adopt_disk_results(&renderer, 0);
    assert(compiled->initialized && !compiled->program);
    for (unsigned id = 0; id < 64; id++) {
        ShaderState key = { id };
        ShaderBinding *binding = container_of(lru_lookup(&renderer.shader_cache, id % 4, &key), ShaderBinding, node);
        if (id == 1) continue;
        assert(binding->cached && binding->program_format == 19);
        assert(binding->program_size == sizeof(unsigned));
        assert(*(unsigned *)binding->program == (id ^ 0x12345678U));
    }
    /* Disabled-cache shutdown still joins and drains, and may be repeated. */
    PGRAPHState pg = { &renderer };
    renderer.shader_disk_thread = sthread_create(publish, NULL);
    assert(renderer.shader_disk_thread);
    renderer.shader_cache_writeback_pending = 1;
    pgraph_gl_shader_write_cache_reload_list(&pg);
    assert(!renderer.shader_disk_thread && !renderer.shader_disk_pending);
    assert(retro_mpsc_empty(&renderer.shader_disk_results));
    assert(renderer.shader_cache_writeback_complete == 1);
    assert(!renderer.shader_cache_writeback_pending);
    pgraph_gl_shader_write_cache_reload_list(&pg);
    assert(renderer.shader_cache_writeback_complete == 2);
    lru_flush(&renderer.shader_cache);

    /* The writer must retain only its snapshot when the live binding changes. */
    ShaderBinding live = {0};
    live.node.hash = 123;
    live.state.id = 42;
    live.program_format = 77;
    live.program_size = 16;
    live.program = malloc(16);
    memset(live.program, 0xab, 16);
    shader_start_disk_save(&live);
    assert(live.save_thread && !live.program);
    live.state.id = 99;
    live.program_size = 0;
    sthread_join(live.save_thread);
    char *name = shader_get_binary_path(directory, 123);
    FILE *file = fopen(name, "rb");
    assert(file);
    unsigned char bytes[256];
    size_t size = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    size_t offset = 4 + 8 + strlen(shader_gl_vendor) + 1 +
                    8 + strlen(shader_gl_version) + 1 + 8;
    GLenum format;
    ShaderState state;
    memcpy(&format, bytes + offset, sizeof(format)); offset += sizeof(format);
    memcpy(&state, bytes + offset, sizeof(state)); offset += sizeof(state);
    size_t program_size;
    memcpy(&program_size, bytes + offset, sizeof(program_size)); offset += sizeof(program_size);
    assert(format == 77 && state.id == 42 && program_size == 16 && size == offset + 16);
    for (unsigned i = 0; i < 16; i++) assert(bytes[offset + i] == 0xab);
    assert(!remove(name));
    free(name);
    puts("Shader cache: 18000 publications, render-only LRU, shutdown joins and immutable disk-save snapshot passed");
    return 0;
}

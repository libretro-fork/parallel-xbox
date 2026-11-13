/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_mpsc.h>
#include <rthreads/rthreads.h>
#include <retro_timers.h>
#include "hw/xbox/nv2a/pgraph/gl/disk-writer.h"
#include "qemu/lru.h"
#include "../../hw/xbox/nv2a/pgraph/gl/shader-binary.h"

static retro_atomic_int_t mapping_closes;
void xemu_file_unmap(XemuFileMap *file)
{
    if (file->file) retro_atomic_fetch_add_int(&mapping_closes, 1);
    free(file->owned);
    *file = (XemuFileMap){0};
}

typedef unsigned GLenum;
typedef struct { unsigned id; } ShaderState;
typedef struct {
    LruNode node;
    ShaderState state;
    bool initialized, cached;
    void *program;
    ShaderBinaryOwner program_owner;
    size_t program_size;
    GLenum program_format;
} ShaderBinding;
typedef struct {
    retro_mpsc_t shader_disk_results;
    retro_atomic_int_t shader_disk_mappings;
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
#define g_new0(type, n) ((type *)calloc(n, sizeof(type)))
static unsigned poisoned_allocations;
static void *poisoned_alloc(size_t size) {
    void *p=malloc(size);assert(p);memset(p,0xa5,size);poisoned_allocations++;return p;
}
#define g_new(type, n) ((type *)poisoned_alloc(sizeof(type) * (n)))
#define g_free free
static retro_atomic_int_t hold_save,save_entered;
static retro_eventcount_t save_entered_event,save_release_event;
static FILE *qemu_fopen(const char *path,const char *mode)
{
    if (!strcmp(mode,"wb") && retro_atomic_load_acquire_int(&hold_save)) {
        assert(!owner);
        retro_atomic_store_release_int(&save_entered,1);
        retro_eventcount_notify(&save_entered_event);
        while(retro_atomic_load_acquire_int(&hold_save)) {
            int key=retro_eventcount_prepare_wait(&save_release_event);
            if(!retro_atomic_load_acquire_int(&hold_save))retro_eventcount_cancel_wait(&save_release_event);
            else retro_eventcount_commit_wait(&save_release_event,key);
        }
    }
    return fopen(path,mode);
}
#define qemu_unlink remove
static void qemu_mkdir(const char *path) { (void)path; }
static char *g_strdup_printf(const char *format,...) {
    va_list ap,copy;va_start(ap,format);va_copy(copy,ap);
    int n=vsnprintf(NULL,0,format,copy);va_end(copy);assert(n>=0);
    char *p=malloc(n+1);assert(p);vsnprintf(p,n+1,format,ap);va_end(ap);return p;
}
static char *shader_get_bin_directory(uint64_t hash) { return strdup(directory); }
static char *shader_get_binary_path(const char *path, uint64_t hash)
{
    size_t size = strlen(directory) + 40;
    char *name = malloc(size);
    assert(name);
    snprintf(name, size, "%s/%llu.bin", directory, (unsigned long long)hash);
    return name;
}
static bool shader_write_cache_version(FILE *file)
{
    return fwrite("TEST", 4, 1, file) == 1;
}
#include "shader-cache-cursor.h"
static bool shader_read_cache_version(FileCursor *c) {
    const void *p=cursor_take(c,4);return p && !memcmp(p,"TEST",4);
}
static unsigned map_shift;
bool xemu_file_map(XemuFileMap *m,const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return false;
    assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>=0);rewind(f);
    unsigned char *base=malloc((size_t)size+32);assert(base);
    assert(fread(base+map_shift,1,size,f)==(size_t)size);fclose(f);
    *m=(XemuFileMap){base+map_shift,(size_t)size,base,base};return true;
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
    shader_binary_release(binding->program, &binding->program_owner);
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
        unsigned *allocation = malloc(2 * sizeof(unsigned));
        assert(allocation);
        allocation[1] = id ^ 0x12345678U;
        XemuFileMap map = { (const uint8_t *)allocation,
                           2 * sizeof(unsigned), allocation, allocation };
        result->program = shader_binary_take_file(&result->program_owner,
            &map, allocation + 1, sizeof(unsigned), &renderer.shader_disk_mappings);
        assert(!map.data);
        retro_mpsc_push(&renderer.shader_disk_results, &result->node);
    }
    retro_atomic_fetch_add_int(&done, 1);
}

static void test_binary_owners(void)
{
    ShaderBinaryOwner owners[SHADER_BINARY_MAX_MAPPINGS + 1];
    void *views[SHADER_BINARY_MAX_MAPPINGS + 1];
    retro_atomic_int_t count = {0};
    for (unsigned i = 0; i <= SHADER_BINARY_MAX_MAPPINGS; i++) {
        unsigned char *allocation = malloc(32);
        memset(allocation, 0x5a, 32);
        XemuFileMap file = {allocation, 32, allocation, allocation};
        views[i] = shader_binary_take_file(&owners[i], &file, allocation + 8,
                                          16, &count);
        assert(!file.data && !file.file && !file.owned);
        if (i < SHADER_BINARY_MAX_MAPPINGS) {
            assert(views[i] == allocation + 8 && owners[i].file.file);
        } else {
            assert(!owners[i].file.data && !owners[i].mapping_count);
        }
        assert(retro_atomic_load_acquire_int(&count) <= SHADER_BINARY_MAX_MAPPINGS);
        for (unsigned j = 0; j < 16; j++)
            assert(((unsigned char *)views[i])[j] == 0x5a);
    }
    for (unsigned i = 0; i <= SHADER_BINARY_MAX_MAPPINGS; i++)
        shader_binary_release(views[i], &owners[i]);
    assert(retro_atomic_load_acquire_int(&count) == 0);

    /* Non-mapping VFS fallback transfers its allocation and interior view. */
    unsigned char *allocation = malloc(32);
    if (!allocation) _Exit(1);
    XemuFileMap file = {allocation, 32, NULL, allocation};
    void *view = shader_binary_take_file(&owners[0], &file, allocation + 8, 16, &count);
    assert(view == allocation + 8 && owners[0].file.owned == allocation);
    assert(!owners[0].mapping_count && !file.data);
    shader_binary_release(view, &owners[0]);
    /* A released slot is available again. */
    allocation = malloc(32);
    if (!allocation) _Exit(1);
    file = (XemuFileMap){allocation, 32, allocation, allocation};
    view = shader_binary_take_file(&owners[0], &file, allocation + 8, 16, &count);
    assert(view == allocation + 8 && owners[0].mapping_count == &count);
    shader_binary_release(view, &owners[0]);
    assert(retro_atomic_load_acquire_int(&count) == 0);
    retro_atomic_store_relaxed_int(&mapping_closes, 0);
}
int main(int argc, char **argv)
{
    sthread_t *threads[8];
    assert(argc == 2);
    directory = argv[1];
    test_binary_owners();
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
    assert(retro_atomic_load_acquire_int(&renderer.shader_disk_mappings) == 0);
    assert(retro_atomic_load_acquire_int(&mapping_closes) == 18000);

    /* The writer must retain only its snapshot when the live binding changes. */
    ShaderBinding live = {0};
    live.node.hash = 123;
    live.state.id = 42;
    live.program_format = 77;
    live.program_size = 16;
    void *allocation = malloc(32);
    XemuFileMap file_owner = {allocation, 32, allocation, allocation};
    live.program = shader_binary_take_file(&live.program_owner, &file_owner,
        (char *)allocation + 16, 16, &renderer.shader_disk_mappings);
    memset(live.program, 0xab, 16);
    retro_atomic_int_init(&hold_save,1);retro_atomic_int_init(&save_entered,0);
    retro_eventcount_init(&save_entered_event);retro_eventcount_init(&save_release_event);
    GLDiskWriter *writer=gl_disk_writer_create(directory,shader_gl_vendor,shader_gl_version,sizeof(ShaderDiskResult));
    unsigned save_allocations=poisoned_allocations;
    assert(writer && shader_start_disk_save(writer,&live));
    assert(poisoned_allocations==save_allocations);
    assert(!live.program);
    while(!retro_atomic_load_acquire_int(&save_entered)) {
        int key=retro_eventcount_prepare_wait(&save_entered_event);
        if(retro_atomic_load_acquire_int(&save_entered))retro_eventcount_cancel_wait(&save_entered_event);
        else retro_eventcount_commit_wait(&save_entered_event,key);
    }
    /* Reuse the live state while the real writer is held before opening. */
    evict(NULL,&live.node);
    live.state.id = 99;
    live.program_size = 0;
    retro_atomic_store_release_int(&hold_save,0);retro_eventcount_notify(&save_release_event);
    gl_disk_writer_destroy(writer);
    retro_eventcount_free(&save_entered_event);retro_eventcount_free(&save_release_event);
    assert(!live.program_owner.file.data);
    assert(retro_atomic_load_acquire_int(&renderer.shader_disk_mappings) == 0);
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
    /* Exercise the production reader before and after its mapping cap.
     * A capped transfer frees input bytes before the result is published. */
    for(unsigned shift=0;shift<32;shift++)for(unsigned capped=0;capped<2;capped++) {
        map_shift=shift;
        retro_atomic_store_relaxed_int(&renderer.shader_disk_mappings,capped?SHADER_BINARY_MAX_MAPPINGS:0);
        unsigned before=poisoned_allocations;
        shader_load_from_disk(&pg,123);assert(poisoned_allocations==before+1);
        shader_adopt_disk_results(&renderer,0);
        ShaderState key={42};
        ShaderBinding *loaded=container_of(lru_lookup(&renderer.shader_cache,123,&key),ShaderBinding,node);
        assert(loaded->cached && loaded->state.id==42 && loaded->program_format==77 && loaded->program_size==16);
        for(unsigned i=0;i<16;i++)assert(((unsigned char *)loaded->program)[i]==0xab);
        assert(!!loaded->program_owner.file.data==!capped);
        lru_flush(&renderer.shader_cache);
        assert(retro_atomic_load_acquire_int(&renderer.shader_disk_mappings)==(capped?SHADER_BINARY_MAX_MAPPINGS:0));
        retro_atomic_store_relaxed_int(&renderer.shader_disk_mappings,0);
    }
    /* Every truncated binary must be rejected without publication. */
    for(size_t n=0;n<size;n++) {
        file=fopen(name,"wb");assert(file);assert(fwrite(bytes,1,n,file)==n);fclose(file);
        shader_load_from_disk(&pg,123);
        assert(retro_mpsc_empty(&renderer.shader_disk_results));
        assert(!renderer.shader_disk_pending);
        assert(retro_atomic_load_acquire_int(&renderer.shader_disk_mappings)==0);
        file=fopen(name,"rb");assert(!file);
    }
    free(name);
    puts("Shader cache: 18000 publications, render-only LRU, shutdown joins and immutable disk-save snapshot, 64 poisoned mapped/capped reader round trips and all binary truncations passed");
    return 0;
}

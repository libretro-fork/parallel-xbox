/*
 * Geforce NV2A PGRAPH OpenGL Renderer
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2020-2025 Matt Borgerson
 * Copyright (c) 2026 Réda Chérif-Touil
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/fast-hash.h"
#include "qemu/file-map.h"
#include "qemu/mstring.h"
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

#include "xemu-version.h"
#include "ui/xemu-settings.h"
#include "hw/xbox/nv2a/pgraph/util.h"
#include "hw/xbox/nv2a/pgraph/seed.h"
#include "hw/xbox/nv2a/pgraph/glsl/uniform-cache.h"
#include "debug.h"
#include "renderer.h"
#include "disk-writer.h"

static const char *shader_gl_vendor = NULL;
static void spec_binding_cancel(PGRAPHGLState *r);

/* xemu_version follows upstream, so fork-only generator changes need their
 * own binary-cache revision. Bump this when generated shader semantics change. */
static const char shader_codegen_version[] = "specular-coefficients-1";

/* Deleting a program frees its GL name for reuse: drop any uniform cache
 * held against that name so a future program cannot inherit it. */
static void delete_program(PGRAPHGLState *r, GLuint prog)
{
    uniform_cache_forget(&r->uni_cache, prog);
    glDeleteProgram(prog);
}
/*
 * The GLSL generators (pgraph_glsl_gen_*) are pure: they read only the state
 * they are handed, so the gen worker runs them with no lock, as the Vulkan
 * renderer's worker does. The generators' family tables and dynamic queues
 * (glsl/psh.c, glsl/vsh.c) are only ever touched on the render thread: by
 * the state setters, the queue pump and adoption.
 */

static GLenum get_gl_primitive_mode(enum ShaderPolygonMode polygon_mode, enum ShaderPrimitiveMode primitive_mode)
{
    switch (primitive_mode) {
    case PRIM_TYPE_POINTS: return GL_POINTS;
    case PRIM_TYPE_LINES: return GL_LINES;
    case PRIM_TYPE_LINE_LOOP: return GL_LINE_LOOP;
    case PRIM_TYPE_LINE_STRIP: return GL_LINE_STRIP;
    case PRIM_TYPE_TRIANGLES: return GL_TRIANGLES;
    case PRIM_TYPE_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
    case PRIM_TYPE_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
    case PRIM_TYPE_QUADS: return GL_LINES_ADJACENCY;
    case PRIM_TYPE_QUAD_STRIP: return GL_LINE_STRIP_ADJACENCY;
    case PRIM_TYPE_POLYGON:
        if (polygon_mode == POLY_MODE_LINE) {
            return GL_LINE_LOOP;
        } else if (polygon_mode == POLY_MODE_FILL) {
            return GL_TRIANGLE_FAN;
        }

        assert(!"PRIM_TYPE_POLYGON with invalid polygon_mode");
        return 0;
    default:
        assert(!"Invalid primitive_mode");
        return 0;
    }
}

static GLuint create_gl_shader(GLenum gl_shader_type,
                               const char *code,
                               const char *name)
{
    GLint compiled = 0;

    NV2A_GL_DGROUP_BEGIN("Creating new %s", name);

    NV2A_DPRINTF("compile new %s, code:\n%s\n", name, code);

    GLuint shader = glCreateShader(gl_shader_type);
    glShaderSource(shader, 1, &code, 0);
    glCompileShader(shader);

    /* Check it compiled */
    compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLchar* log;
        GLint log_length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        log = g_malloc(log_length * sizeof(GLchar));
        glGetShaderInfoLog(shader, log_length, NULL, log);
        fprintf(stderr, "%s\n\n" "nv2a: %s compilation failed: %s\n", code, name, log);
        g_free(log);

        NV2A_GL_DGROUP_END();
        abort();
    }

    NV2A_GL_DGROUP_END();

    return shader;
}

/* Set at context init (ui/xemu.c): GL 4.1 and a passing self-test (probe.c),
 * as 4.0-era drivers expose broken separable shaders (MEASURED: Intel HD
 * 520, driver 21.20.16.4526). */
int xemu_gl_opt_capable = 1;

/* With GPU boost, each stage compiles once into a separable program,
 * combined by pipeline objects, instead of one full link per stage
 * combination. */
static bool gl_sso_enabled(void)
{
    static int on;
    if (unlikely(!on)) {
        on = -1;
        if (pgraph_gpu_boost_gl()) {
            /* The stages are GLSL 4.10, which a 4.0 context may reject:
             * both GL 4.1 and the extension are required. */
            if (xemu_gl_version() < 41) {
                fprintf(stderr, "nv2a: GL %d.%d render context, using linked"
                                " programs\n", xemu_gl_version() / 10,
                        xemu_gl_version() % 10);
            } else if (glo_check_extension("GL_ARB_separate_shader_objects")) {
                on = 1;
            } else {
                fprintf(stderr, "nv2a: GL_ARB_separate_shader_objects missing,"
                                " using linked programs\n");
            }
        }
    }
    return on > 0;
}

static void set_texture_sampler_uniforms(ShaderBinding *binding)
{
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        char samplerName[16];
        snprintf(samplerName, sizeof(samplerName), "texSamp%d", i);
        GLint texSampLoc =
            glGetUniformLocation(binding->gl_program, samplerName);
        if (texSampLoc >= 0) {
            glUniform1i(texSampLoc, i);
        }
    }
}

static void update_shader_uniform_locs(ShaderBinding *binding)
{
    char tmp[64];

    for (int i = 0; i < ARRAY_SIZE(binding->uniform_locs.vsh); i++) {
        const char *name = VshUniformInfo[i].name;
        if (VshUniformInfo[i].count > 1) {
            snprintf(tmp, sizeof(tmp), "%s[0]", name);
            name = tmp;
        }
        binding->uniform_locs.vsh[i] = glGetUniformLocation(binding->gl_program, name);
    }

    for (int i = 0; i < ARRAY_SIZE(binding->uniform_locs.psh); i++) {
        const char *name = PshUniformInfo[i].name;
        if (PshUniformInfo[i].count > 1) {
            snprintf(tmp, sizeof(tmp), "%s[0]", name);
            name = tmp;
        }
        binding->uniform_locs.psh[i] = glGetUniformLocation(binding->gl_program, name);
    }

    GLuint prog = binding->gl_program;
    binding->uniform_locs.gsh.surfaceSize = glGetUniformLocation(prog, "gsSurfaceSize");
    binding->uniform_locs.gsh.lineWidth = glGetUniformLocation(prog, "gsLineWidth");
    binding->uniform_locs.gsh.keepWinding = glGetUniformLocation(prog, "gsKeepWinding");
}

static char *module_get_dir(uint64_t hash)
{
    return g_strdup_printf("%s/shader_modules/%04x",
                           xemu_settings_get_base_path(),
                           (uint32_t)(hash >> 48));
}

static char *module_get_path(const char *dir, uint64_t hash)
{
    return g_strdup_printf("%s/%012" PRIx64, dir,
                           hash & ~((uint64_t)0xffff << 48));
}

/* A cached file, mapped, read front to back: what is taken is pointed at
 * where it lies, nothing copied. */
typedef struct FileCursor {
    const uint8_t *p;
    size_t left;
} FileCursor;

static const void *cursor_take(FileCursor *c, size_t n)
{
    const uint8_t *p = c->p;
    if (n > c->left) {
        return NULL;
    }
    c->p += n;
    c->left -= n;
    return p;
}

static bool cursor_take_u64(FileCursor *c, uint64_t *v)
{
    const void *p = cursor_take(c, sizeof(*v));
    if (p) {
        memcpy(v, p, sizeof(*v));
    }
    return p != NULL;
}

/* A length-prefixed NUL-terminated string as the file keeps it, equal to
 * expect (its NUL where the file says the string ends) */
static bool cursor_take_string_eq(FileCursor *c, const char *expect)
{
    uint64_t len;
    const char *str;
    if (!cursor_take_u64(c, &len) || len == 0 || len > 256) {
        return false;
    }
    str = cursor_take(c, len);
    return str && strlen(expect) == len - 1 &&
           memcmp(str, expect, len) == 0;
}

static bool shader_read_cache_version(FileCursor *c)
{
    return cursor_take_string_eq(c, xemu_version) &&
           cursor_take_string_eq(c, shader_codegen_version);
}

static bool shader_write_cache_version(FILE *f)
{
    const char *versions[] = { xemu_version, shader_codegen_version };
    for (size_t i = 0; i < ARRAY_SIZE(versions); i++) {
        uint64_t len = strlen(versions[i]) + 1;
        if (fwrite(&len, sizeof(len), 1, f) != 1 ||
            fwrite(versions[i], len, 1, f) != 1) {
            return false;
        }
    }
    return true;
}

/* The header of a cached module file: upstream/generator versions and GL
 * vendor, then its key. Reject a different revision/driver or truncated file. */
/* The key can be unaligned in the file. Borrow its bytes only while mapped. */
static const void *module_read_header_view(FileCursor *c)
{
    uint64_t len;
    if (!shader_read_cache_version(c) ||
        !cursor_take_string_eq(c, shader_gl_vendor) ||
        !cursor_take_u64(c, &len) || len != sizeof(ShaderModuleCacheKey)) {
        return NULL;
    }
    return cursor_take(c, sizeof(ShaderModuleCacheKey));
}

/* Prewarming needs an owned key after its file mapping is released. */
static bool module_read_header(FileCursor *c, ShaderModuleCacheKey *key)
{
    const void *bytes = module_read_header_view(c);
    if (!bytes) {
        return false;
    }
    memcpy(key, bytes, sizeof(*key));
    return true;
}

/* Separable stage programs are cached individually: one stage feeds many
 * combinations, so the per-binding cache would store it once per pipeline. */
static bool module_load_from_disk(PGRAPHGLState *r,
                                  ShaderModuleCacheEntry *module,
                                  uint64_t hash)
{
    char *dir = module_get_dir(hash);
    char *path = module_get_path(dir, hash);
    g_free(dir);

    XemuFileMap m;
    if (!xemu_file_map(&m, path)) {
        g_free(path);
        return false;
    }

    bool ok = false;
    FileCursor c = { m.data, m.len };
    const void *fmt;
    const void *bin;
    uint64_t len;
    GLenum format;
    const void *cached_key;
    GLuint program = 0;

    cached_key = module_read_header_view(&c);
    if (!cached_key ||
        memcmp(cached_key, &module->key, sizeof(module->key)) != 0) {
        goto out;
    }
    fmt = cursor_take(&c, sizeof(format));
    if (!fmt || !cursor_take_u64(&c, &len) || len == 0 ||
        len > (64 << 20)) {
        goto out;
    }
    memcpy(&format, fmt, sizeof(format));
    bin = cursor_take(&c, len);
    if (!bin) {
        goto out;
    }

    /* The driver takes the binary from the mapped file */
    (void)glGetError();
    program = glCreateProgram();
    glProgramBinary(program, format, bin, len);
    /* This admission path immediately resolves uniform locations, which
     * already requires completed linking. Reject unusable cache binaries at
     * that boundary instead of publishing an invalid stage to the LRU. */
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (glGetError() != GL_NO_ERROR || !linked) {
        goto out;
    }
    module->gl_stage_program = program;
    ok = true;

out:
    xemu_file_unmap(&m);
    if (!ok) {
        if (program) {
            /* This fresh handle was never published to the uniform cache. */
            glDeleteProgram(program);
        }
        qemu_unlink(path);
    }
    g_free(path);
    return ok;
}

/* An acquired disk slot alone owns these bytes. Growth discards old
 * contents rather than copying a binary the driver will replace. */
static void *module_binary_reserve(GLDiskSlot *slot, size_t size)
{
    if (size > slot->capacity) {
        size_t grown = slot->capacity <= SIZE_MAX / 2 ?
                       slot->capacity * 2 : size;
        slot->capacity = MAX(size, grown);
        g_free(slot->scratch);
        slot->scratch = g_malloc(slot->capacity);
    }
    return slot->scratch;
}

typedef struct ModuleDiskSave {
    ShaderModuleCacheKey key;
    uint64_t hash;
    GLenum format;
    GLsizei size;
} ModuleDiskSave;

static void module_write_to_disk(GLDiskSlot *slot)
{
    ModuleDiskSave *save=slot->data;
    char *parent = g_strdup_printf("%s/shader_modules",
                                   slot->base_path);
    qemu_mkdir(parent);
    g_free(parent);

    char *dir = g_strdup_printf("%s/shader_modules/%04x",
                                slot->base_path, (uint32_t)(save->hash >> 48));
    qemu_mkdir(dir);
    char *path = module_get_path(dir, save->hash);
    g_free(dir);

    FILE *f = qemu_fopen(path, "wb");
    if (!f) {
        g_free(path);
        g_free(save);
        return;
    }

    bool ok = shader_write_cache_version(f);
#define WR(p, n) ok = ok && fwrite(p, n, 1, f) == 1

    uint64_t len = strlen(slot->vendor) + 1;
    WR(&len, sizeof(len));
    WR(slot->vendor, len);

    len = sizeof(save->key);
    WR(&len, sizeof(len));
    WR(&save->key, len);

    WR(&save->format, sizeof(save->format));
    len = save->size;
    WR(&len, sizeof(len));
    WR(slot->scratch, len);

#undef WR

    ok = fclose(f) == 0 && ok;
    if (!ok) {
        qemu_unlink(path);
    }
    g_free(path);
    g_free(save);
}

static bool module_save_to_disk(PGRAPHGLState *r,
                                ShaderModuleCacheEntry *module, uint64_t hash)
{
    GLDiskSlot *slot=gl_disk_writer_acquire(r->cache_writer);
    if (!slot) return false;
    GLint size=0;
    glGetProgramiv(module->gl_stage_program, GL_PROGRAM_BINARY_LENGTH, &size);
    if (size <= 0) return true;
    void *bin=module_binary_reserve(slot,size);
    GLsizei copied=0;
    GLenum format;
    glGetProgramBinary(module->gl_stage_program,size,&copied,&format,bin);
    if (glGetError()!=GL_NO_ERROR || copied<=0 || copied>size) return true;
    ModuleDiskSave *save=g_new(ModuleDiskSave,1);
    save->key=module->key;
    save->hash=hash;
    save->format=format;
    save->size=copied;
    slot->data=save;slot->write=module_write_to_disk;
    gl_disk_writer_submit(r->cache_writer);
    return true;
}

/* Uniform locations belong to the stage program, not to the combination that
 * uses it: resolve them once here instead of at every pipeline assembly. */
static void module_resolve_uniform_locs(ShaderModuleCacheEntry *module)
{
    char tmp[64];
    GLuint prog = module->gl_stage_program;

    if (module->key.kind == GL_VERTEX_SHADER) {
        for (int i = 0; i < ARRAY_SIZE(module->locs.vsh); i++) {
            const char *name = VshUniformInfo[i].name;
            if (VshUniformInfo[i].count > 1) {
                snprintf(tmp, sizeof(tmp), "%s[0]", name);
                name = tmp;
            }
            module->locs.vsh[i] = glGetUniformLocation(prog, name);
        }
        return;
    }

    if (module->key.kind == GL_GEOMETRY_SHADER) {
        GshUniformLocs *g = &module->locs.gsh;
        g->surfaceSize = glGetUniformLocation(prog, "gsSurfaceSize");
        g->lineWidth = glGetUniformLocation(prog, "gsLineWidth");
        g->keepWinding = glGetUniformLocation(prog, "gsKeepWinding");
        return;
    }

    if (module->key.kind != GL_FRAGMENT_SHADER) {
        return;
    }

    for (int i = 0; i < ARRAY_SIZE(module->locs.psh); i++) {
        const char *name = PshUniformInfo[i].name;
        if (PshUniformInfo[i].count > 1) {
            snprintf(tmp, sizeof(tmp), "%s[0]", name);
            name = tmp;
        }
        module->locs.psh[i] = glGetUniformLocation(prog, name);
    }

    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        snprintf(tmp, sizeof(tmp), "texSamp%d", i);
        GLint loc = glGetUniformLocation(prog, tmp);
        if (loc >= 0) {
            glProgramUniform1i(prog, loc, i);
        }
    }
}

static bool gl_has_parallel_compile(void)
{
    static int has = -1;
    if (has == -1) {
        has = glo_check_extension("GL_ARB_parallel_shader_compile");
    }
    return has;
}

static void module_locs_demand(ShaderModuleCacheEntry *module);

static void module_save_queue_push(PGRAPHGLState *r,
                                   ShaderModuleCacheEntry *module)
{
    if (r->module_save_w - r->module_save_r <
        ARRAY_SIZE(r->module_save_queue)) {
        r->module_save_queue[r->module_save_w %
                             ARRAY_SIZE(r->module_save_queue)] = module;
        r->module_save_w++;
    }
}

static void module_save_queue_drain(PGRAPHGLState *r, unsigned max_items)
{
    unsigned items = 0;
    while (r->module_save_r != r->module_save_w) {
        if (items++ >= max_items) {
            break;
        }
        ShaderModuleCacheEntry *module =
            r->module_save_queue[r->module_save_r %
                                 ARRAY_SIZE(r->module_save_queue)];
        /* Evicted since queueing (stage program dropped): skip. */
        if (module->gl_stage_program) {
            if (gl_has_parallel_compile()) {
                GLint done = GL_TRUE;
                glGetProgramiv(module->gl_stage_program,
                               GL_COMPLETION_STATUS_ARB, &done);
                if (!done) {
                    /* Head still linking: retry next flip. */
                    break;
                }
            }
            /* Link done: finalizing locations here is free and keeps the
             * first draw that uses this module from paying it. */
            module_locs_demand(module);
            if (!module_save_to_disk(r, module,
                                    fast_hash((void *)&module->key,
                                              sizeof(ShaderModuleCacheKey)))) {
                break; /* Disk slots busy: retry without exporting or waiting. */
            }
        }
        r->module_save_r++;
    }
}

/* Waits on this module's link only, then resolves its uniform locations:
 * call it before attaching the stage to a pipeline or reading them. */
static void module_locs_demand(ShaderModuleCacheEntry *module)
{
    if (!module->locs_pending) {
        return;
    }
    GLint ok = 0;
    glGetProgramiv(module->gl_stage_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLchar log[2048];
        glGetProgramInfoLog(module->gl_stage_program, 2048, NULL, log);
        fprintf(stderr, "nv2a: stage program (kind %x) failed: %s\n",
                module->key.kind, log);
        abort();
    }
    module_resolve_uniform_locs(module);
    if (module->gl_shader) {
        glDetachShader(module->gl_stage_program, module->gl_shader);
        glDeleteShader(module->gl_shader);
        module->gl_shader = 0;
    }
    module->locs_pending = false;
}

static void shader_module_cache_entry_init(Lru *lru, LruNode *node,
                                           const void *key)
{
    PGRAPHGLState *r = container_of(lru, PGRAPHGLState, shader_module_cache);
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    memcpy(&module->key, key, sizeof(ShaderModuleCacheKey));

    /* Adopt only for pipelines: the linked-program path attaches the
     * shader object, which an adopted module lacks. */
    if (gl_sso_enabled() && r->comb_adopt_program &&
        fast_hash((void *)key, sizeof(ShaderModuleCacheKey)) ==
            r->comb_adopt_hash) {
        module->gl_stage_program = r->comb_adopt_program;
        r->comb_adopt_program = 0;
        module->gl_shader = 0;
        module->locs_pending = false;
        module_resolve_uniform_locs(module);
        if (g_config.perf.cache_shaders) {
            module_save_queue_push(r, module);
        }
        return;
    }

    if (gl_sso_enabled() && g_config.perf.cache_shaders) {
        uint64_t hash = fast_hash((void *)key, sizeof(ShaderModuleCacheKey));
        bool hit = module_load_from_disk(r, module, hash);
        if (hit) {
            module->gl_shader = 0;
            module->locs_pending = false;
            module_resolve_uniform_locs(module);
            return;
        }
    }

    const char *kind_str;
    MString *code;

    /* Separable stages match by explicit interface locations, a process-wide
     * choice left out of the cache key. */
    bool locs = gl_sso_enabled();
    switch (module->key.kind) {
    case GL_VERTEX_SHADER: {
        kind_str = "vertex shader";
        GenVshGlslOptions o = module->key.vsh.glsl_opts;
        o.locations = locs;
        code = pgraph_glsl_gen_vsh(&module->key.vsh.state, o);
        break;
    }
    case GL_GEOMETRY_SHADER: {
        kind_str = "geometry shader";
        GenGeomGlslOptions o = module->key.geom.glsl_opts;
        o.locations = locs;
        code = pgraph_glsl_gen_geom(&module->key.geom.state, o);
        break;
    }
    case GL_FRAGMENT_SHADER: {
        kind_str = "fragment shader";
        GenPshGlslOptions o = module->key.psh.glsl_opts;
        o.locations = locs;
        code = pgraph_glsl_gen_psh(&module->key.psh.state, o);
        break;
    }
    default:
        assert(!"Invalid shader module kind");
        kind_str = "unknown";
        code = NULL;
    }

    if (!code) {
        /* A geometry key whose state needs no geometry stage (a stale key
         * from disk or a seed): the module stays empty, as in the worker. */
        module->gl_shader = 0;
        module->gl_stage_program = 0;
        module->locs_pending = false;
        return;
    }

    if (gl_sso_enabled()) {
        /* Compile and link without a status query, so the driver's
         * compiler threads overlap; module_locs_demand() waits. Not
         * glCreateShaderProgramv: it reads COMPILE_STATUS internally. */
        const char *src = mstring_get_str(code);
        GLuint sh;
        GLuint prog = pgraph_gl_separable_program(module->key.kind, src, &sh);
        /* Detach and delete wait for module_locs_demand(): touching the
         * shader now can make the driver finish the link inline. */
        module->gl_stage_program = prog;
        module->gl_shader = sh;
        module->locs_pending = true;
        if (g_config.perf.cache_shaders) {
            module_save_queue_push(r, module);
        }
    } else {
        module->gl_stage_program = 0;
        module->locs_pending = false;
        module->gl_shader =
            create_gl_shader(module->key.kind, mstring_get_str(code), kind_str);
    }
    mstring_unref(code);
}

static void shader_module_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    if (module->gl_stage_program) {
        /* Pipelines built from this stage still reference it; a linked
         * program owned its own copy, a separable one does not. */
        if (module->gl_shader) {
            /* Never finalized: drop the pending shader object too. */
            glDeleteShader(module->gl_shader);
            module->gl_shader = 0;
        }
        module->gl_stage_program = 0;
        module->locs_pending = false;
    } else {
        glDeleteShader(module->gl_shader);
    }
}

static bool shader_module_cache_entry_compare(Lru *lru, LruNode *node,
                                              const void *key)
{
    ShaderModuleCacheEntry *module =
        container_of(node, ShaderModuleCacheEntry, node);
    return memcmp(&module->key, key, sizeof(ShaderModuleCacheKey));
}

/* One stage compiled and linked alone as a separable program, without
 * asking the driver for any status (that would wait for the compile). */
GLuint pgraph_gl_separable_program(GLenum kind, const char *src,
                                   GLuint *shader)
{
    GLuint sh = glCreateShader(kind);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLuint prog = glCreateProgram();
    glProgramParameteri(prog, GL_PROGRAM_SEPARABLE, GL_TRUE);
    if (g_config.perf.cache_shaders) {
        glProgramParameteri(prog, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    }
    glAttachShader(prog, sh);
    glLinkProgram(prog);
    *shader = sh;
    return prog;
}

/* The module keys of a state: geometry (kind 0 unless the state needs one),
 * vertex, fragment. Returns whether the geometry stage is needed. */
static bool state_module_keys(const ShaderState *st,
                              ShaderModuleCacheKey keys[3])
{
    bool need_gs = pgraph_glsl_need_geom(&st->geom);

    if (need_gs) {
        memset(keys, 0, 3 * sizeof(keys[0]));
        keys[0].kind = GL_GEOMETRY_SHADER;
        keys[0].geom.state = st->geom;
        keys[0].geom.glsl_opts.ui_clamp_stage = pgraph_glsl_ui_clamp_stage(&st->psh);
    } else {
        /* Callers skip this key; only its kind tag is meaningful. */
        keys[0].kind = 0;
        memset(&keys[1], 0, 2 * sizeof(keys[0]));
    }
    keys[1].kind = GL_VERTEX_SHADER;
    keys[1].vsh.state = st->vsh;
    keys[1].vsh.glsl_opts.prefix_outputs = need_gs;
    keys[2].kind = GL_FRAGMENT_SHADER;
    keys[2].psh.state = st->psh;
    return need_gs;
}

static ShaderModuleCacheEntry *get_shader_module_for_key(
    PGRAPHGLState *r, const ShaderModuleCacheKey *key)
{
    uint64_t hash = fast_hash((void *)key, sizeof(ShaderModuleCacheKey));
    LruNode *node = lru_lookup(&r->shader_module_cache, hash, key);
    return container_of(node, ShaderModuleCacheEntry, node);
}

static void generate_shaders(PGRAPHGLState *r, ShaderBinding *binding)
{
    GLuint program = gl_sso_enabled() ? 0 : glCreateProgram();
    if (program && g_config.perf.cache_shaders &&
        (xemu_gl_version() >= 41 ||
         glo_check_extension("GL_ARB_get_program_binary"))) {
        glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    }

    ShaderState *state = &binding->state;
    ShaderModuleCacheKey keys[3];

    binding->gl_primitive_mode = get_gl_primitive_mode(
        state->geom.polygon_front_mode, state->geom.primitive_mode);
    binding->gl_prog_gs = 0;

    /* Submit all of this state's modules before waiting on any: new
     * modules then cost the longest link, not the sum. */
    bool need_geometry_shader = state_module_keys(state, keys);
    ShaderModuleCacheEntry *gs_mod = NULL;
    if (need_geometry_shader) {
        gs_mod = get_shader_module_for_key(r, &keys[0]);
        if (!gl_sso_enabled()) {
            glAttachShader(program, gs_mod->gl_shader);
        }
    }

    /* create the vertex shader */
    ShaderModuleCacheEntry *vs_mod = get_shader_module_for_key(r, &keys[1]);
    if (!gl_sso_enabled()) {
        glAttachShader(program, vs_mod->gl_shader);
    }

    /* generate a fragment shader from register combiners */
    ShaderModuleCacheEntry *fs_mod = get_shader_module_for_key(r, &keys[2]);
    if (!gl_sso_enabled()) {
        glAttachShader(program, fs_mod->gl_shader);
    }

    if (gl_sso_enabled()) {
        binding->uniform_locs.gsh = (GshUniformLocs){ -1, -1, -1 };
        if (gs_mod) {
            module_locs_demand(gs_mod);
            binding->gl_prog_gs = gs_mod->gl_stage_program;
            binding->uniform_locs.gsh = gs_mod->locs.gsh;
        }
        module_locs_demand(vs_mod);
        binding->gl_prog_vs = vs_mod->gl_stage_program;
        memcpy(binding->uniform_locs.vsh, vs_mod->locs.vsh,
               sizeof(VshUniformLocs));
        module_locs_demand(fs_mod);
        binding->gl_prog_fs = fs_mod->gl_stage_program;
        memcpy(binding->uniform_locs.psh, fs_mod->locs.psh,
               sizeof(PshUniformLocs));
    }

    if (gl_sso_enabled()) {
        glGenProgramPipelines(1, &binding->gl_pipeline);
        glUseProgramStages(binding->gl_pipeline, GL_VERTEX_SHADER_BIT,
                           binding->gl_prog_vs);
        glUseProgramStages(binding->gl_pipeline, GL_FRAGMENT_SHADER_BIT,
                           binding->gl_prog_fs);
        if (binding->gl_prog_gs) {
            glUseProgramStages(binding->gl_pipeline, GL_GEOMETRY_SHADER_BIT,
                               binding->gl_prog_gs);
        }
        binding->gl_program = 0;
        glUseProgram(0);
        glBindProgramPipeline(binding->gl_pipeline);
        binding->initialized = true;
        return;
    }
    /* link the program */
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if(!linked) {
        GLchar log[2048];
        glGetProgramInfoLog(program, 2048, NULL, log);
        fprintf(stderr, "nv2a: shader linking failed: %s\n", log);
        abort();
    }

    glUseProgram(program);

    binding->gl_program = program;
    binding->initialized = true;

    set_texture_sampler_uniforms(binding);

    /* validate the program */
    GLint valid = 0;
    glValidateProgram(program);
    glGetProgramiv(program, GL_VALIDATE_STATUS, &valid);
    if (!valid) {
        GLchar log[1024];
        glGetProgramInfoLog(program, 1024, NULL, log);
        fprintf(stderr, "nv2a: shader validation failed: %s\n", log);
        abort();
    }

    update_shader_uniform_locs(binding);
}

static const char *shader_gl_version = NULL;

static void shader_create_cache_folder(void)
{
    char *shader_path = g_strdup_printf("%sshaders_boost",
                                        xemu_settings_get_base_path());
    qemu_mkdir(shader_path);
    g_free(shader_path);
}

/* "_boost": a second GPU, since removed, kept its own cache under the
 * plain names; the names stay so the caches already on disk stay valid. */
static char *shader_get_lru_cache_path(void)
{
    return g_strdup_printf("%s/shader_cache_list_boost",
                           xemu_settings_get_base_path());
}

/* Cold-boot shader seeds (file I/O in pgraph/seed.c). The disk caches hold
 * driver binaries, lost with any driver or xemu update. A seed keeps the
 * driver-independent keys of the states a game drew (seed-warmed bindings
 * never count), merged into its file at shutdown up to a fixed cap. At boot
 * their module keys feed the async worker in idle flips, and pipelines are
 * built only from resident modules, never with a synchronous compile. */

struct SeedCollect {
    GArray *pipes;
    GArray *mods;
};

/* Derive exactly the module keys generate_shaders() will ask for. */
static void seed_collect_binding(Lru *lru, LruNode *node, void *opaque)
{
    struct SeedCollect *c = opaque;
    ShaderBinding *b = container_of(node, ShaderBinding, node);
    if (!b->initialized || b->uses == 0) {
        return;
    }
    g_array_append_val(c->pipes, b->state);
    ShaderModuleCacheKey keys[3];
    bool need_gs = state_module_keys(&b->state, keys);
    g_array_append_vals(c->mods, need_gs ? &keys[0] : &keys[1],
                        need_gs ? 3 : 2);
}

static void seed_write_files(PGRAPHGLState *r)
{
    if (!g_config.perf.cache_shaders || !gl_sso_enabled()) {
        return;
    }
    struct SeedCollect c = {
        .pipes = g_array_new(FALSE, FALSE, sizeof(ShaderState)),
        .mods = g_array_new(FALSE, FALSE, sizeof(ShaderModuleCacheKey)),
    };
    lru_visit_active(&r->shader_cache, seed_collect_binding, &c);
    /* Union with the file: a short session must not shrink a seed. */
    if (c.pipes->len) {
        unsigned n_pipes, n_mods;
        unsigned new_states = pgraph_seed_merge(
            PGRAPH_SEED_GL_STATES, sizeof(ShaderState), c.pipes->data,
            c.pipes->len, 4096, &n_pipes);
        unsigned new_mods = pgraph_seed_merge(
            PGRAPH_SEED_GL_MODULES, sizeof(ShaderModuleCacheKey),
            c.mods->data, c.mods->len, 8192, &n_mods);
        if (new_states || new_mods) {
            fprintf(stderr,
                    "nv2a: shader seed written (%u states, %u module keys, "
                    "%u new)\n", n_pipes, n_mods, new_states);
        }
    }
    g_array_free(c.pipes, TRUE);
    g_array_free(c.mods, TRUE);
}

/* Disk parsing never touches GL objects or the render-owned LRU. */
typedef struct ShaderDiskResult {
    retro_mpsc_node_t node;
    uint64_t hash;
    ShaderState state;
    GLenum program_format;
    size_t program_size;
    void *program;
    ShaderBinaryOwner program_owner;
} ShaderDiskResult;

/* Limit live-frame adoption; a joined worker can be drained with limit=0. */
static void shader_adopt_disk_results(PGRAPHGLState *r, unsigned limit)
{
    for (unsigned n = 0; !limit || n < limit; n++) {
        if (!r->shader_disk_pending) {
            r->shader_disk_pending = retro_mpsc_take_all(&r->shader_disk_results);
        }
        retro_mpsc_node_t *node = r->shader_disk_pending;
        if (!node) {
            break;
        }
        ShaderDiskResult *result = container_of(node, ShaderDiskResult, node);
        r->shader_disk_pending = node->next;
        LruNode *entry = lru_lookup(&r->shader_cache, result->hash, &result->state);
        ShaderBinding *binding = container_of(entry, ShaderBinding, node);
        if (!binding->initialized && !binding->program) {
            binding->program_format = result->program_format;
            binding->program_size = result->program_size;
            binding->program = result->program;
            binding->program_owner = result->program_owner;
            binding->cached = true;
        } else {
            shader_binary_release(result->program, &result->program_owner);
        }
        g_free(result);
    }
}

static void shader_write_lru_list_entry_to_disk(Lru *lru, LruNode *node, void *opaque)
{
    FILE *lru_list_file = (FILE*) opaque;
    size_t written = fwrite(&node->hash, sizeof(uint64_t), 1, lru_list_file);
    if (written != 1) {
        fprintf(stderr, "nv2a: Failed to write shader list entry %llx to disk\n",
                (unsigned long long) node->hash);
    }
}

void pgraph_gl_shader_write_cache_reload_list(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    /* Join before draining, including when caching was disabled mid-session.
     * A later writeback/finalize must not join an already-consumed handle. */
    if (r->shader_disk_thread) {
        sthread_join(r->shader_disk_thread);
        r->shader_disk_thread = NULL;
    }
    shader_adopt_disk_results(r, 0);

    if (!g_config.perf.cache_shaders) {
        pgraph_clear_and_notify(&r->shader_cache_writeback_complete,
                                &r->shader_cache_writeback_pending);
        return;
    }

    char *shader_lru_path = shader_get_lru_cache_path();

    FILE *lru_list = qemu_fopen(shader_lru_path, "wb");
    g_free(shader_lru_path);
    if (!lru_list) {
        fprintf(stderr, "nv2a: Failed to open shader LRU cache for writing\n");
        /* the waiter is released either way: nothing more is written */
        pgraph_clear_and_notify(&r->shader_cache_writeback_complete,
                                &r->shader_cache_writeback_pending);
        return;
    }

    lru_visit_active(&r->shader_cache, shader_write_lru_list_entry_to_disk, lru_list);
    fclose(lru_list);

    seed_write_files(r);

    lru_flush(&r->shader_cache);

    pgraph_clear_and_notify(&r->shader_cache_writeback_complete,
                            &r->shader_cache_writeback_pending);
}

bool pgraph_gl_shader_load_from_memory(ShaderBinding *binding)
{
    assert(glGetError() == GL_NO_ERROR);

    if (!binding->program) {
        return false;
    }

    GLuint gl_program = glCreateProgram();
    glProgramBinary(gl_program, binding->program_format, binding->program,
                    binding->program_size);
    shader_binary_release(binding->program, &binding->program_owner);
    binding->program = NULL;
    GLint gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        NV2A_DPRINTF(
            "failed to load shader binary from disk: GL error code %d\n",
            gl_error);
        glDeleteProgram(gl_program);
        return false;
    }

    GLint link_status = GL_FALSE;
    glGetProgramiv(gl_program, GL_LINK_STATUS, &link_status);
    if (!link_status) {
        NV2A_DPRINTF(
            "failed to load shader binary from disk: link status is FALSE\n");
        glDeleteProgram(gl_program);
        return false;
    }

    glUseProgram(gl_program);

    binding->gl_program = gl_program;
    binding->gl_primitive_mode =
        get_gl_primitive_mode(binding->state.geom.polygon_front_mode,
                              binding->state.geom.primitive_mode);
    binding->initialized = true;

    set_texture_sampler_uniforms(binding);

    glValidateProgram(gl_program);
    GLint valid = 0;
    glGetProgramiv(gl_program, GL_VALIDATE_STATUS, &valid);
    if (!valid) {
        GLchar log[1024];
        glGetProgramInfoLog(gl_program, 1024, NULL, log);
        NV2A_DPRINTF("failed to load shader binary from disk: %s\n", log);
        glDeleteProgram(gl_program);
        return false;
    }

    update_shader_uniform_locs(binding);

    return true;
}

static char *shader_get_bin_directory(uint64_t hash)
{
    const char *cfg_dir = xemu_settings_get_base_path();
    char *shader_bin_dir =
        g_strdup_printf("%s/shaders_boost/%04x", cfg_dir,
                        (uint32_t)(hash >> 48));
    return shader_bin_dir;
}

static char *shader_get_binary_path(const char *shader_bin_dir, uint64_t hash)
{
    uint64_t bin_mask = (uint64_t)0xffff << 48;
    return g_strdup_printf("%s/%012" PRIx64, shader_bin_dir, hash & ~bin_mask);
}

static void shader_load_from_disk(PGRAPHState *pg, uint64_t hash)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    char *shader_bin_dir = shader_get_bin_directory(hash);
    char *shader_path = shader_get_binary_path(shader_bin_dir, hash);
    void *program_buffer = NULL;
    XemuFileMap m = { 0 };
    FileCursor c;
    const void *p;
    uint64_t cached_state_size;
    GLenum program_binary_format;
    const void *state;
    size_t shader_size;

    g_free(shader_bin_dir);

    /* Transfer the file owner with its binary view. A bounded fallback
     * copies only when retaining another mapped file would exceed the cap. */
    if (!xemu_file_map(&m, shader_path)) {
        goto error;
    }
    c.p = m.data;
    c.left = m.len;

    if (!shader_read_cache_version(&c) ||
        !cursor_take_string_eq(&c, shader_gl_vendor) ||
        !cursor_take_string_eq(&c, shader_gl_version)) {
        goto error;
    }
    /* The cached blob embeds a raw ShaderState: reject files written by
     * a build whose layout differs, or its size fields are garbage. */
    if (!cursor_take_u64(&c, &cached_state_size) ||
        cached_state_size != sizeof(ShaderState)) {
        goto error;
    }
    p = cursor_take(&c, sizeof(program_binary_format));
    if (!p) {
        goto error;
    }
    memcpy(&program_binary_format, p, sizeof(program_binary_format));
    state = cursor_take(&c, sizeof(ShaderState));
    if (!state) {
        goto error;
    }
    p = cursor_take(&c, sizeof(shader_size));
    if (!p) {
        goto error;
    }
    memcpy(&shader_size, p, sizeof(shader_size));
    p = cursor_take(&c, shader_size);
    if (!p || !shader_size || shader_size > INT_MAX) {
        goto error;
    }
    /* Every field is assigned below; MPSC push initializes the link. */
    ShaderDiskResult *result = g_new(ShaderDiskResult, 1);
    /* The mapping-limit fallback may unmap immediately: own the state first. */
    memcpy(&result->state, state, sizeof(result->state));
    program_buffer = shader_binary_take_file(&result->program_owner, &m, p,
                                             shader_size, &r->shader_disk_mappings);
    g_free(shader_path);
    result->hash = hash;
    result->program_format = program_binary_format;
    result->program_size = shader_size;
    result->program = program_buffer;
    retro_mpsc_push(&r->shader_disk_results, &result->node);
    return;

error:
    /* Delete the shader so it won't be loaded again */
    xemu_file_unmap(&m);
    qemu_unlink(shader_path);
    g_free(shader_path);
    g_free(program_buffer);
}

static void shader_reload_lru_from_disk(void *arg)
{
    if (!g_config.perf.cache_shaders) {
        return;
    }

    PGRAPHState *pg = (PGRAPHState*) arg;
    char *shader_lru_path = shader_get_lru_cache_path();

    FILE *lru_shaders_list = qemu_fopen(shader_lru_path, "rb");
    g_free(shader_lru_path);
    if (!lru_shaders_list) {
        return;
    }

    uint64_t hash;
    while (fread(&hash, sizeof(uint64_t), 1, lru_shaders_list) == 1) {
        shader_load_from_disk(pg, hash);
    }

    fclose(lru_shaders_list);
}

static void shader_cache_entry_init(Lru *lru, LruNode *node, const void *state)
{
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    memcpy(&binding->state, state, sizeof(ShaderState));
    binding->initialized = false;
    binding->cached = false;
    binding->program = NULL;
    binding->program_owner = (ShaderBinaryOwner){0};
    binding->uses = 0;
    binding->gl_program = 0;
    binding->gl_pipeline = 0;
    binding->gl_prog_vs = 0;
    binding->gl_prog_fs = 0;
    binding->gl_prog_gs = 0;
    binding->gl_primitive_mode = 0;
}

static void shader_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    ShaderBinding *binding = container_of(node, ShaderBinding, node);

    /* The evicted entry may be the one the renderer still points at: its
     * program and pipeline are deleted right below, and a later restore
     * (the off-frame texture touch rebinds the live program) would then
     * name a deleted object -- GL_INVALID_OPERATION, reported by AMD's
     * driver as "GL error 0x502 at touch fresh textures". The next draw
     * looks the binding up again, so dropping it here costs nothing. */
    PGRAPHGLState *r = container_of(lru, PGRAPHGLState, shader_cache);
    if (r->shader_binding == binding) {
        r->shader_binding = NULL;
    }
    if (r->spec_binding == binding) {
        spec_binding_cancel(r);
    }
    binding->initialized = false;

    glDeleteProgram(binding->gl_program);
    if (binding->gl_pipeline) {
        glDeleteProgramPipelines(1, &binding->gl_pipeline);
        binding->gl_pipeline = 0;
    }
    if (binding->program) {
        shader_binary_release(binding->program, &binding->program_owner);
    }

    binding->cached = false;
    binding->program = NULL;
    memset(&binding->state, 0, sizeof(ShaderState));
}

static bool shader_cache_entry_compare(Lru *lru, LruNode *node, const void *key)
{
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    return memcmp(&binding->state, key, sizeof(ShaderState));
}

/* Refresh driver-owned strings whenever a new GL context is initialized. */
static void shader_capture_gl_identity(void)
{
    shader_gl_vendor = (const char *)glGetString(GL_VENDOR);
    if (!shader_gl_vendor) {
        shader_gl_vendor = "Unknown vendor";
    }
    shader_gl_version = (const char *)glGetString(GL_VERSION);
    if (!shader_gl_version) {
        shader_gl_version = "Unknown version";
    }
}

void pgraph_gl_init_shaders(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    retro_mpsc_init(&r->shader_disk_results);
    retro_atomic_store_relaxed_int(&r->shader_disk_mappings, 0);
    r->shader_disk_pending = NULL;
    retro_eventcount_init(&r->shader_cache_writeback_complete);

    shader_capture_gl_identity();

    shader_create_cache_folder();

    /* FIXME: Make this configurable */
    const size_t shader_cache_size = 50*1024;
    lru_init(&r->shader_cache);
    r->shader_cache_entries = malloc(shader_cache_size * sizeof(ShaderBinding));
    assert(r->shader_cache_entries != NULL);
    for (int i = 0; i < shader_cache_size; i++) {
        lru_add_free(&r->shader_cache, &r->shader_cache_entries[i].node);
    }

    r->shader_cache.init_node = shader_cache_entry_init;
    r->shader_cache.compare_nodes = shader_cache_entry_compare;
    r->shader_cache.post_node_evict = shader_cache_entry_post_evict;

    r->shader_disk_thread = sthread_create(shader_reload_lru_from_disk, pg);
    if (!r->shader_disk_thread) {
        abort();
    }

    /* FIXME: Make this configurable */
    const size_t shader_module_cache_size = 50*1024;
    lru_init(&r->shader_module_cache);
    r->shader_module_cache_entries =
        g_malloc_n(shader_module_cache_size, sizeof(ShaderModuleCacheEntry));
    assert(r->shader_module_cache_entries != NULL);
    for (int i = 0; i < shader_module_cache_size; i++) {
        lru_add_free(&r->shader_module_cache, &r->shader_module_cache_entries[i].node);
    }

    r->shader_module_cache.init_node = shader_module_cache_entry_init;
    r->shader_module_cache.compare_nodes = shader_module_cache_entry_compare;
    r->shader_module_cache.post_node_evict = shader_module_cache_entry_post_evict;

    r->cache_writer=gl_disk_writer_create(xemu_settings_get_base_path(),
                                                  shader_gl_vendor, shader_gl_version);
    r->seed.state = -1;
    r->prewarm.state = -1;
}

static void module_prewarm_finish(PGRAPHGLState *r)
{
    if (r->prewarm.bucket) {
        g_dir_close(r->prewarm.bucket);
    }
    if (r->prewarm.root) {
        g_dir_close(r->prewarm.root);
    }
    r->prewarm.bucket = r->prewarm.root = NULL;
    g_free(r->prewarm.bucket_path);
    g_free(r->prewarm.base);
    r->prewarm.bucket_path = r->prewarm.base = NULL;
    r->prewarm.state = 0;
}

void pgraph_gl_finalize_shaders(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    /* What the flip services hold: the texture touch objects, the seed and
     * prewarm traversal. Links in flight stay for the next GL renderer. */
    glDeleteProgram(r->touch_prog);
    glDeleteVertexArrays(1, &r->touch_vao);
    g_free(r->seed.mod_keys);
    g_free(r->seed.pipe_states);
    module_prewarm_finish(r);

    // Clear out shader cache
    pgraph_gl_shader_write_cache_reload_list(pg); // FIXME: also flushes, rename for clarity
    lru_flush(&r->shader_cache);
    free(r->shader_cache_entries);
    r->shader_cache_entries = NULL;

    lru_flush(&r->shader_module_cache);
    g_free(r->shader_module_cache_entries);
    r->shader_module_cache_entries = NULL;
    gl_disk_writer_destroy(r->cache_writer);
    r->cache_writer=NULL;
}

static void shader_write_to_disk(GLDiskSlot *slot)
{
    ShaderDiskResult *binding = slot->data;

    char *shader_bin = g_strdup_printf("%s/shaders_boost/%04x", slot->base_path,
                                       (uint32_t)(binding->hash >> 48));
    char *shader_path = shader_get_binary_path(shader_bin, binding->hash);

    const uint64_t gl_vendor_len = strlen(slot->vendor) + 1;
    const uint64_t gl_version_len = strlen(slot->version) + 1;

    qemu_mkdir(shader_bin);
    g_free(shader_bin);

    FILE *shader_file = qemu_fopen(shader_path, "wb");
    if (!shader_file) {
        goto error;
    }

    size_t written;
    #define WRITE_OR_ERR(data, data_size) \
        do { \
            written = fwrite(data, data_size, 1, shader_file); \
            if (written != 1) { \
                fclose(shader_file); \
                goto error; \
            } \
        } while (0)

    if (!shader_write_cache_version(shader_file)) {
        fclose(shader_file);
        goto error;
    }

    WRITE_OR_ERR(&gl_vendor_len, sizeof(gl_vendor_len));
    WRITE_OR_ERR(slot->vendor, gl_vendor_len);

    WRITE_OR_ERR(&gl_version_len, sizeof(gl_version_len));
    WRITE_OR_ERR(slot->version, gl_version_len);

    {
        uint64_t state_size = sizeof(binding->state);
        WRITE_OR_ERR(&state_size, sizeof(state_size));
    }
    WRITE_OR_ERR(&binding->program_format, sizeof(binding->program_format));
    WRITE_OR_ERR(&binding->state, sizeof(binding->state));

    WRITE_OR_ERR(&binding->program_size, sizeof(binding->program_size));
    WRITE_OR_ERR(binding->program, binding->program_size);

    #undef WRITE_OR_ERR

    if (fclose(shader_file) != 0) {
        goto error;
    }

    g_free(shader_path);
    shader_binary_release(binding->program, &binding->program_owner);
    g_free(binding);

    return;

error:
    fprintf(stderr, "nv2a: Failed to write shader binary file to %s\n", shader_path);
    qemu_unlink(shader_path);
    g_free(shader_path);
    shader_binary_release(binding->program, &binding->program_owner);
    g_free(binding);
    return;
}

/* Publish an immutable snapshot; eviction never waits for a disk writer. */
static bool shader_start_disk_save(GLDiskWriter *writer, ShaderBinding *binding)
{
    GLDiskSlot *slot=gl_disk_writer_acquire(writer);
    if (!slot) return false;
    ShaderDiskResult *save=g_new(ShaderDiskResult,1);
    save->hash=binding->node.hash;
    save->state=binding->state;
    save->program_format=binding->program_format;
    save->program_size=binding->program_size;
    save->program=binding->program;
    save->program_owner=binding->program_owner;
    binding->program=NULL;
    binding->program_owner=(ShaderBinaryOwner){0};
    slot->data=save;slot->write=shader_write_to_disk;
    gl_disk_writer_submit(writer);
    return true;
}

void pgraph_gl_shader_cache_to_disk(PGRAPHGLState *r, ShaderBinding *binding)
{
    if (binding->cached || !gl_disk_writer_acquire(r->cache_writer)) {
        return;
    }

    GLint program_size;
    glGetProgramiv(binding->gl_program, GL_PROGRAM_BINARY_LENGTH, &program_size);

    if (binding->program) {
        shader_binary_release(binding->program, &binding->program_owner);
        binding->program = NULL;
    }

    /* program_size might be zero on some systems, if no binary formats are supported */
    if (program_size <= 0) {
        return;
    }

    binding->program = g_malloc(program_size);
    GLsizei program_size_copied = 0;
    glGetProgramBinary(binding->gl_program, program_size, &program_size_copied,
                       &binding->program_format, binding->program);
    if (glGetError() != GL_NO_ERROR || program_size_copied <= 0 ||
        program_size_copied > program_size) {
        shader_binary_release(binding->program, &binding->program_owner);
        binding->program = NULL;
        return;
    }

    binding->program_size = program_size_copied;
    binding->cached = true;

    binding->cached=shader_start_disk_save(r->cache_writer,binding);
}

/* With GPU boost, refill only the uniform sections written since, and skip
 * uniforms unchanged since the last upload to that GL program; keyed on the
 * program, as bindings share stage programs (glsl/uniform-cache.h). */
static bool uniform_dirty_enabled(void)
{
    return pgraph_gpu_boost_gl();
}

/* At every pgraph flush (snapshot load, reset, scale change): the source
 * arrays may have changed behind every cache. */
void pgraph_gl_uniform_sources_invalidate(PGRAPHGLState *r)
{
    r->vsh_uni_src.valid = false;
    r->vsh_uni_src.ff_valid = false;
    uniform_cache_invalidate(&r->uni_cache);
    /* Restored program words/registers need a fresh shader-state selection;
     * do not rely on an incidental whole-register dirty write to wake it. */
    r->shader_binding = NULL;
    spec_binding_cancel(r);
}

/* prog 0: the program bound with glUseProgram. */
static void gl_upload_uniform(void *opaque, const UniformInfo *info, int loc,
                              const void *value)
{
    GLuint prog = *(const GLuint *)opaque;
    int n = info->count;

    nv2a_profile_inc_counter(NV2A_PROF_UNIFORM_UP);
    switch (info->type) {
    case UniformElementType_uint:
        if (prog) {
            glProgramUniform1uiv(prog, loc, n, value);
        } else {
            glUniform1uiv(loc, n, value);
        }
        break;
    case UniformElementType_int:
        if (prog) {
            glProgramUniform1iv(prog, loc, n, value);
        } else {
            glUniform1iv(loc, n, value);
        }
        break;
    case UniformElementType_ivec2:
        if (prog) {
            glProgramUniform2iv(prog, loc, n, value);
        } else {
            glUniform2iv(loc, n, value);
        }
        break;
    case UniformElementType_ivec4:
        if (prog) {
            glProgramUniform4iv(prog, loc, n, value);
        } else {
            glUniform4iv(loc, n, value);
        }
        break;
    case UniformElementType_float:
        if (prog) {
            glProgramUniform1fv(prog, loc, n, value);
        } else {
            glUniform1fv(loc, n, value);
        }
        break;
    case UniformElementType_vec2:
        if (prog) {
            glProgramUniform2fv(prog, loc, n, value);
        } else {
            glUniform2fv(loc, n, value);
        }
        break;
    case UniformElementType_vec3:
        if (prog) {
            glProgramUniform3fv(prog, loc, n, value);
        } else {
            glUniform3fv(loc, n, value);
        }
        break;
    case UniformElementType_vec4:
        if (prog) {
            glProgramUniform4fv(prog, loc, n, value);
        } else {
            glUniform4fv(loc, n, value);
        }
        break;
    case UniformElementType_mat2:
        if (prog) {
            glProgramUniformMatrix2fv(prog, loc, n, GL_FALSE, value);
        } else {
            glUniformMatrix2fv(loc, n, GL_FALSE, value);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void apply_uniform_updates(PGRAPHGLState *r, const UniformInfo *info,
                                  int *locs, void *values, size_t count,
                                  GLuint prog, const int8_t *sections)
{
    int cs = -1;
    if (uniform_dirty_enabled() && prog) {
        cs = uniform_cache_slot(&r->uni_cache, prog);
    }

    /* Only judge this function's own calls. */
    (void)glGetError();
    uniform_cache_apply(&r->uni_cache, cs, r->vsh_uni_src.gen, info, locs,
                        values, count, sections, gl_upload_uniform, &prog);
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        fprintf(stderr, "uniform error 0x%x prog=%u\n", e, prog);
    }
    assert(e == GL_NO_ERROR);
}

/* Per draw: the wide-line geometry shader needs the guest line width, the
 * surface size that turns it into clip-space units, and which winding the
 * culling state lets through (the hardware never culls a line). */
void pgraph_gl_wide_line_uniforms(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;
    ShaderBinding *b = r->shader_binding;
    GshUniformLocs *l = &b->uniform_locs.gsh;

    unsigned int aa_width = 1, aa_height = 1;
    pgraph_apply_anti_aliasing_factor(pg, &aa_width, &aa_height);
    float size[2] = { (float)pg->surface_binding_dim.width / aa_width,
                      (float)pg->surface_binding_dim.height / aa_height };
    float width = pg->line_width / 8.0f;

    /* draw.c poses glFrontFace(CW) when the FRONTFACE bit is set. */
    uint32_t setupraster = pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER);
    int keep = 2; /* either winding */
    if (setupraster & NV_PGRAPH_SETUPRASTER_CULLENABLE) {
        bool front_ccw = !(setupraster & NV_PGRAPH_SETUPRASTER_FRONTFACE);
        switch (GET_MASK(setupraster, NV_PGRAPH_SETUPRASTER_CULLCTRL)) {
        case 1: /* front faces culled: draw a back face */
            keep = front_ccw ? 0 : 1;
            break;
        case 2: /* back faces culled: draw a front face */
            keep = front_ccw ? 1 : 0;
            break;
        default:
            break;
        }
    }

    GLuint prog = gl_sso_enabled() ? b->gl_prog_gs : 0;
    if (prog) {
        glProgramUniform2fv(prog, l->surfaceSize, 1, size);
        glProgramUniform1f(prog, l->lineWidth, width);
        glProgramUniform1i(prog, l->keepWinding, keep);
    } else {
        glUniform2fv(l->surfaceSize, 1, size);
        glUniform1f(l->lineWidth, width);
        glUniform1i(l->keepWinding, keep);
    }
}

// FIXME: Consider UBO to align with VK renderer
static void update_shader_uniforms(PGRAPHState *pg, ShaderBinding *binding)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    GLuint vs_prog = gl_sso_enabled() ? binding->gl_prog_vs : 0;
    if (uniform_dirty_enabled()) {
        pgraph_vsh_uniform_source_refresh(pg, &r->vsh_uni_src,
                                          &binding->state.vsh,
                                          binding->uniform_locs.vsh);
        apply_uniform_updates(r, VshUniformInfo, binding->uniform_locs.vsh,
                              &r->vsh_uni_src.values, VshUniform__COUNT,
                              vs_prog, pgraph_vsh_uniform_section);
    } else {
        VshUniformValues vsh_values;
        pgraph_glsl_set_vsh_uniform_values(pg, &binding->state.vsh,
                                      binding->uniform_locs.vsh, &vsh_values);
        apply_uniform_updates(r, VshUniformInfo, binding->uniform_locs.vsh,
                              &vsh_values, VshUniform__COUNT, vs_prog, NULL);
    }

    PshUniformValues psh_values;
    pgraph_glsl_set_psh_uniform_values(pg, binding->uniform_locs.psh, &psh_values);

    for (int i = 0; i < 4; i++) {
        if (r->texture_binding[i] != NULL) {
            float scale = r->texture_binding[i]->scale;
            /* Sign tags encoded F16 RAM texels without extending uniforms. */
            if ((binding->state.psh.shadow_map[i] & 2) &&
                r->texture_binding[i]->s2t_format != GL_DEPTH_COMPONENT24) {
                scale = -scale;
            }
            psh_values.texScale[i] = scale;
        }
    }
    apply_uniform_updates(r, PshUniformInfo, binding->uniform_locs.psh,
                          &psh_values, PshUniform__COUNT,
                          gl_sso_enabled() ? binding->gl_prog_fs : 0, NULL);
}

/* Links in flight off the draw path, adopted into the module cache once
 * done; only then does a varying family switch over to its interpreter.
 * Their names belong to the render context, which lives for the whole
 * session: a link a closing GL renderer leaves in flight, the next adopts. */
static struct {
    ShaderModuleCacheKey key;
    GLuint gl_shader;
    GLuint gl_program;
    bool used;
    bool gate;
    /* Cold-boot seed key: adopt into the module cache only, with no family
     * ready/adopted notification (nothing is waiting on it). */
    bool seed;
} comb_async[32];

/* With GPU boost, an unseen specialised state draws with its dynamic
 * sibling (same image, state in uniforms) while its own module compiles. */
static bool first_meet_enabled(void)
{
    return pgraph_gpu_boost_gl();
}

#define SPEC_QUEUED 1
#define SPEC_ADOPTED 2
#define SPEC_PIPE_RESERVED 3

static int spec_track_find(PGRAPHGLState *r, uint64_t h)
{
    for (unsigned i = 0; i < ARRAY_SIZE(r->spec_track); i++) {
        if (r->spec_track[i].phase && r->spec_track[i].hash == h) {
            return i;
        }
    }
    return -1;
}

static void spec_track_adopted(PGRAPHGLState *r, const PshState *st)
{
    uint64_t h = fast_hash((const uint8_t *)st, sizeof(*st));
    int i = spec_track_find(r, h);
    if (i >= 0 && r->spec_track[i].phase != SPEC_PIPE_RESERVED) {
        r->spec_track[i].phase = SPEC_ADOPTED;
    }
}

static void spec_queue(PGRAPHGLState *r, uint64_t h, const ShaderState *st)
{
    if (r->spec_pending_w - r->spec_pending_r >=
        ARRAY_SIZE(r->spec_pending)) {
        return;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(r->spec_track); i++) {
        if (!r->spec_track[i].phase) {
            r->spec_track[i].hash = h;
            r->spec_track[i].phase = SPEC_QUEUED;
            r->spec_pending[r->spec_pending_w %
                            ARRAY_SIZE(r->spec_pending)] = *st;
            r->spec_pending_w++;
            return;
        }
    }
}



/* GLSL generation worker: sources are CPU string work, kept off the render
 * thread, which only submits finished ones to GL. Jobs go by key; holding
 * no GL name, the worker and its jobs outlive a renderer. */
#define GEN_JOBS 16
static struct {
    ShaderModuleCacheKey key;
    MString *code; /* worker writes before publishing GEN_COMPLETE */
    retro_atomic_int_t state;
    bool used;
    bool gate;
    bool seed;
} gen_jobs[GEN_JOBS];
enum { GEN_IDLE, GEN_QUEUED, GEN_COMPLETE };
static retro_eventcount_t gen_ready;
static sthread_t *gen_thr;
static bool gen_thr_up;

/* The render thread owns keys and bookkeeping. The worker reads a slot
 * only after QUEUED publication and returns its source with COMPLETE.
 * No slot is recycled until the render thread acquires COMPLETE. */
static bool gen_job_generate(int i)
{
    if (retro_atomic_load_acquire_int(&gen_jobs[i].state) != GEN_QUEUED) {
        return false;
    }
    /* QUEUED acquisition lends this immutable slot through generation.
     * The render thread cannot recycle its key until it acquires COMPLETE;
     * no generator retains the state after returning. */
    const ShaderModuleCacheKey *key = &gen_jobs[i].key;
    bool locs = gl_sso_enabled();
    GenVshGlslOptions vo = key->vsh.glsl_opts;
    GenGeomGlslOptions go = key->geom.glsl_opts;
    GenPshGlslOptions po = key->psh.glsl_opts;
    vo.locations = go.locations = po.locations = locs;
    MString *code =
        key->kind == GL_VERTEX_SHADER ?
            pgraph_glsl_gen_vsh(&key->vsh.state, vo) :
        key->kind == GL_GEOMETRY_SHADER ?
            pgraph_glsl_gen_geom(&key->geom.state, go) :
            pgraph_glsl_gen_psh(&key->psh.state, po);
    if (!code) {
        /* A stale seed key may request an unnecessary geometry shader. */
        code = mstring_from_str("");
    }
    gen_jobs[i].code = code;
    retro_atomic_store_release_int(&gen_jobs[i].state, GEN_COMPLETE);
    return true;
}

static bool gen_jobs_pending(void)
{
    for (int i = 0; i < GEN_JOBS; i++) {
        if (retro_atomic_load_acquire_int(&gen_jobs[i].state) == GEN_QUEUED) {
            return true;
        }
    }
    return false;
}

static void shader_gen_worker(void *arg)
{
    for (;;) {
        bool did = false;
        for (int i = 0; i < GEN_JOBS; i++) {
            did |= gen_job_generate(i);
        }
        if (!did) {
            int key = retro_eventcount_prepare_wait(&gen_ready);
            if (gen_jobs_pending()) {
                retro_eventcount_cancel_wait(&gen_ready);
            } else {
                retro_eventcount_commit_wait(&gen_ready, key);
            }
        }
    }
}

/* Render thread only. True if queued (or already present). */
static bool gen_job_push(const ShaderModuleCacheKey *key, bool gate,
                                bool seed)
{
    for (int i = 0; i < GEN_JOBS; i++) {
        if (gen_jobs[i].used &&
            memcmp(&gen_jobs[i].key, key, sizeof(*key)) == 0) {
            return true;
        }
    }
    for (int i = 0; i < GEN_JOBS; i++) {
        if (!gen_jobs[i].used) {
            gen_jobs[i].key = *key;
            gen_jobs[i].gate = gate;
            gen_jobs[i].seed = seed;
            gen_jobs[i].code = NULL;
            gen_jobs[i].used = true;
            retro_atomic_store_release_int(&gen_jobs[i].state, GEN_QUEUED);
            return true;
        }
    }
    return false;
}

/* Only the render thread changes used; the worker publishes completion but
 * cannot consume these slots between this check and a multi-module enqueue. */
static bool gen_jobs_have_free(unsigned needed)
{
    for (int i = 0; i < GEN_JOBS; i++) {
        if (!gen_jobs[i].used && --needed == 0) {
            return true;
        }
    }
    return false;
}

/* Already generated, queued, or cached? Skip regenerating. */
static bool module_known(PGRAPHGLState *r, const ShaderModuleCacheKey *key)
{
    uint64_t hash = fast_hash((const void *)key, sizeof(*key));
    Lru *cache = &r->shader_module_cache;
    unsigned bin = lru_hash_to_bin(cache, hash);
    LruNode *node;
    QTAILQ_FOREACH(node, &cache->bins[bin], next_bin) {
        ShaderModuleCacheEntry *module =
            container_of(node, ShaderModuleCacheEntry, node);
        if (node->hash == hash && !memcmp(&module->key, key, sizeof(*key))) {
            return true;
        }
    }
    return false;
}

/* A fragment adoption marker alone cannot release a full draw recipe.
 * Keys are temporary; resident modules keep their native cache owners. */
static bool state_modules_known(PGRAPHGLState *r, const ShaderState *state)
{
    ShaderModuleCacheKey keys[3];
    bool need_gs = state_module_keys(state, keys);
    return (!need_gs || module_known(r, &keys[0])) &&
           module_known(r, &keys[1]) && module_known(r, &keys[2]);
}

/* Render-thread-only query: a cache hit can still have an unfinished link.
 * Borrow only for this predicate; no cache entry escapes or changes owner. */
static bool module_ready(PGRAPHGLState *r, const ShaderModuleCacheKey *key)
{
    uint64_t hash = fast_hash((const uint8_t *)key, sizeof(*key));
    Lru *cache = &r->shader_module_cache;
    unsigned bin = lru_hash_to_bin(cache, hash);
    LruNode *node;
    QTAILQ_FOREACH(node, &cache->bins[bin], next_bin) {
        ShaderModuleCacheEntry *module =
            container_of(node, ShaderModuleCacheEntry, node);
        if (node->hash == hash && !memcmp(&module->key, key, sizeof(*key))) {
            return module->gl_stage_program && !module->locs_pending;
        }
    }
    return false;
}

static int spec_binding_reservation(PGRAPHGLState *r)
{
    ShaderBinding *pending = r->spec_binding;
    if (!pending || pending->state.psh.combiner_dynamic) {
        return -1;
    }
    uint64_t ph = fast_hash((const uint8_t *)&pending->state.psh,
                             sizeof(pending->state.psh));
    int t = spec_track_find(r, ph);
    return t >= 0 && r->spec_track[t].phase == SPEC_PIPE_RESERVED ? t : -1;
}

static void spec_binding_cancel(PGRAPHGLState *r)
{
    int t = spec_binding_reservation(r);
    if (t >= 0) {
        r->spec_track[t].phase = 0;
    }
    r->spec_binding = NULL;
}

static bool spec_binding_track(PGRAPHGLState *r, ShaderBinding *target)
{
    ShaderBinding *pending = r->spec_binding;
    if (pending && pending != target && spec_binding_reservation(r) >= 0) {
        if (pending->dyn_gen == pgraph_glsl_dynamic_gen &&
            pending->state.geom.wide_lines == r->wide_lines) {
            return false;
        }
        spec_binding_cancel(r);
    }
    target->dyn_gen = pgraph_glsl_dynamic_gen;
    r->spec_binding = target;
    return true;
}

static bool state_modules_ready(PGRAPHGLState *r, const ShaderState *state)
{
    ShaderModuleCacheKey keys[3];
    bool need_gs = state_module_keys(state, keys);
    return (!need_gs || module_ready(r, &keys[0])) &&
           module_ready(r, &keys[1]) && module_ready(r, &keys[2]);
}

/* owned_state is the draw's unpublished local capture, never a cache/job or
 * guest view. Project only PSH metadata in place and restore every byte.
 * Vertex microcode/geometry stay borrowed; a hit lends its native cache key
 * through synchronous promotion, with no miss initialization or eviction. */
static ShaderBinding *ready_cover_binding(PGRAPHGLState *r,
                                         ShaderState *owned_state)
{
    PshState exact;
    memcpy(&exact, &owned_state->psh, sizeof(exact));
    ShaderBinding *cover = NULL;
    if (pgraph_glsl_psh_canonicalize_dynamic(&owned_state->psh)) {
        uint64_t hash = fast_hash((const uint8_t *)owned_state,
                                  sizeof(*owned_state));
        Lru *cache = &r->shader_cache;
        unsigned bin = lru_hash_to_bin(cache, hash);
        LruNode *node;
        QTAILQ_FOREACH(node, &cache->bins[bin], next_bin) {
            ShaderBinding *entry = container_of(node, ShaderBinding, node);
            if (node->hash == hash && entry->initialized &&
                entry->gl_pipeline &&
                !memcmp(&entry->state, owned_state, sizeof(*owned_state))) {
                LruNode *hit = lru_lookup(cache, hash, &entry->state);
                cover = container_of(hit, ShaderBinding, node);
                break;
            }
        }
    }
    memcpy(&owned_state->psh, &exact, sizeof(exact));
    return cover;
}

/* One already-owned shader recipe, serviced between frames. The render
 * thread alone mutates both caches: ready lookups below cannot evict a
 * module before generate_shaders consumes it. No missing stage is compiled. */
static void spec_binding_pump(PGRAPHGLState *r)
{
    ShaderBinding *target = r->spec_binding;
    if (!target) {
        return;
    }
    if (!gl_sso_enabled() || !first_meet_enabled() ||
        target->dyn_gen != pgraph_glsl_dynamic_gen ||
        target->state.geom.wide_lines != r->wide_lines) {
        spec_binding_cancel(r);
        return;
    }
    if (!target->initialized) {
        ShaderModuleCacheKey keys[3];
        bool need_gs = state_module_keys(&target->state, keys);
        if ((need_gs && !module_ready(r, &keys[0])) ||
            !module_ready(r, &keys[1]) || !module_ready(r, &keys[2])) {
            if (spec_binding_reservation(r) >= 0) {
                spec_binding_cancel(r);
            }
            return;
        }
        generate_shaders(r, target);
        /* Pipeline setup binds its result; keep the live fallback truthful. */
        glUseProgram(0);
        glBindProgramPipeline(r->shader_binding ?
                              r->shader_binding->gl_pipeline : 0);
    }
    r->spec_binding = NULL;
    if (!target->state.psh.combiner_dynamic) {
        uint64_t ph = fast_hash((const uint8_t *)&target->state.psh,
                                 sizeof(target->state.psh));
        int t = spec_track_find(r, ph);
        /* The ready pipeline no longer needs a scheduling marker. Pending
         * CPU/GL jobs keep their independent owned slots until consumption. */
        if (t >= 0) {
            r->spec_track[t].phase = 0;
        }
    }
    if (r->shader_binding && !target->state.psh.combiner_dynamic) {
        /* Request one real-state selection through the existing fast-path
         * guard for a specialization. A prepared cover serves future misses;
         * it need not recapture the already-correct live exact shader. */
        r->shader_binding->dyn_gen = pgraph_glsl_dynamic_gen - 1;
    }
}

static void comb_async_adopted(PGRAPHGLState *r,
                               const ShaderModuleCacheKey *key,
                               bool gate, bool seed)
{
    if (seed) {
        return; /* Seeds fill the cache without switching a live family. */
    }
    if (key->kind == GL_FRAGMENT_SHADER) {
        if (gate) {
            pgraph_glsl_psh_dynamic_ready(&key->psh.state);
        } else {
            spec_track_adopted(r, &key->psh.state);
        }
    } else if (key->kind == GL_VERTEX_SHADER) {
        pgraph_glsl_vsh_dynamic_ready(&key->vsh.state);
    }
}

static void comb_async_submit(PGRAPHGLState *r, unsigned i, unsigned slot)
{
    /* COMPLETE was acquired by the caller. Keep this immutable job owned
     * until GL consumes its source and the async slot owns its final key,
     * or until the ready resident stage receives its adoption notification. */
    const ShaderModuleCacheKey *key = &gen_jobs[i].key;
    MString *code = gen_jobs[i].code;
    if (module_ready(r, key)) {
        comb_async_adopted(r, key, gen_jobs[i].gate, gen_jobs[i].seed);
    } else {
        const char *src = mstring_get_str(code);
        if (src[0]) {
            GLuint sh;
            GLuint prog = pgraph_gl_separable_program(key->kind, src, &sh);
            comb_async[slot].key = *key;
            comb_async[slot].gate = gen_jobs[i].gate;
            comb_async[slot].seed = gen_jobs[i].seed;
            comb_async[slot].gl_shader = sh;
            comb_async[slot].gl_program = prog;
            comb_async[slot].used = true;
        }
    }
    mstring_unref(code);
    gen_jobs[i].code = NULL;
    gen_jobs[i].used = false;
    retro_atomic_store_relaxed_int(&gen_jobs[i].state, GEN_IDLE);
}

static bool comb_async_take(PGRAPHGLState *r, PshState *out, bool *gate)
{
    if (pgraph_glsl_psh_dynamic_pending_take(out)) {
        *gate = true;
        return true;
    }
    if (r->spec_pending_r != r->spec_pending_w) {
        /* Async compiles slow the synchronous ones down in the driver
         * (MEASURED): at most 12 speculative ones in flight. */
        int in_flight = 0;
        for (unsigned i = 0; i < ARRAY_SIZE(comb_async); i++) {
            in_flight += comb_async[i].used && !comb_async[i].gate;
        }
        if (in_flight >= 12 || !gen_jobs_have_free(3)) {
            return false;
        }
        *out = r->spec_pending[r->spec_pending_r %
                               ARRAY_SIZE(r->spec_pending)].psh;
        *gate = false;
        return true;
    }
    return false;
}

/* The input is owned: compact accepted siblings over records already read.
 * Only the returned prefix is live; no borrowed seed data is modified. */
static unsigned seed_compact_dynamic_states(ShaderState *states, unsigned count)
{
    unsigned used = 0;
    GHashTable *seen = pgraph_seed_hash_set_new();
    for (unsigned i = 0; i < count; i++) {
        ShaderState *state = &states[i];
        if (pgraph_glsl_psh_canonicalize_dynamic(&state->psh) &&
            pgraph_seed_hash_set_add(
                seen, fast_hash((const uint8_t *)state, sizeof(*state)))) {
            if (used != i) {
                memcpy(&states[used], state, sizeof(*state));
            }
            used++;
        }
    }
    g_hash_table_destroy(seen);
    return used;
}

static void seed_load(PGRAPHGLState *r)
{
    if (!pgraph_seed_game_tag()) {
        return; /* the boot has not named the game yet: retry next flip */
    }
    r->seed.state = 0;
    /* Only the dynamic siblings of the learned states are seeded: few
     * cover programs serve many states, so they build unnoticed during the
     * boot and first_meet finds its cover resident; the full set would
     * lag the boot. */
    if (!g_config.perf.cache_shaders || !g_config.perf.shader_seeding ||
        !gl_sso_enabled()) {
        return;
    }
    /* States supersede stored module keys: materialize keys only when no
     * usable states exist. Both requests share mapping and dictionary I/O. */
    PGRAPHSeedRead reads[] = {
        { .section = PGRAPH_SEED_GL_STATES, .itemsize = sizeof(ShaderState) },
        { .section = PGRAPH_SEED_GL_MODULES,
          .itemsize = sizeof(ShaderModuleCacheKey) },
    };
    pgraph_seed_read_first(reads, ARRAY_SIZE(reads));
    r->seed.pipe_states = reads[0].items;
    r->seed.pipe_n = reads[0].count;
    r->seed.pipe_cursor = 0;
    r->seed.mod_keys = reads[1].items;
    r->seed.mod_n = reads[1].count;
    const char *origin = reads[0].dict_count || reads[1].dict_count ?
                         "local + embedded dictionary" : "local";
    if (r->seed.pipe_states) {
        /* Reduce to the dynamic-sibling set: same derivation as the
         * first_meet cover path, deduplicated. */
        ShaderState *sib = r->seed.pipe_states;
        unsigned n_sib = seed_compact_dynamic_states(sib, r->seed.pipe_n);
        r->seed.pipe_n = n_sib;
        /* Module keys for exactly those siblings. */
        g_free(r->seed.mod_keys);
        /* Accepted keys are copied in full; only the n_keys prefix is live. */
        r->seed.mod_keys = g_new(ShaderModuleCacheKey, n_sib * 3);
        unsigned n_keys = 0;
        GHashTable *seen = pgraph_seed_hash_set_new();
        for (unsigned i = 0; i < n_sib; i++) {
            ShaderModuleCacheKey keys[3];
            bool need_gs = state_module_keys(&sib[i], keys);
            for (unsigned k = need_gs ? 0 : 1; k < 3; k++) {
                if (pgraph_seed_hash_set_add(
                        seen, fast_hash((const uint8_t *)&keys[k],
                                        sizeof(keys[k])))) {
                    memcpy(&r->seed.mod_keys[n_keys++], &keys[k], sizeof(keys[k]));
                }
            }
        }
        g_hash_table_destroy(seen);
        r->seed.mod_n = n_keys;
        origin = "dynamic siblings";
    }
    if (r->seed.mod_n || r->seed.pipe_n) {
        r->seed.state = 1;
        fprintf(stderr,
                "nv2a: shader seed loaded (%u module keys, %u states, %s)\n",
                r->seed.mod_n, r->seed.pipe_n, origin);
    }
}

static void seed_finish(PGRAPHGLState *r, const char *why)
{
    g_free(r->seed.mod_keys);
    r->seed.mod_keys = NULL;
    g_free(r->seed.pipe_states);
    r->seed.pipe_states = NULL;
    r->seed.state = 0;
    fprintf(stderr, "nv2a: shader seed pump %s (%u pipelines built)\n",
            why, r->seed.pipes_built);
}

/* Seed keys still travelling through the worker or the driver's compile
 * queue: leftovers are not stalled while any of these can still land. */
static bool seed_jobs_in_flight(PGRAPHGLState *r)
{
    for (int i = 0; i < GEN_JOBS; i++) {
        if (gen_jobs[i].used && gen_jobs[i].seed) {
            return true;
        }
    }
    for (unsigned i = 0; i < ARRAY_SIZE(comb_async); i++) {
        if (comb_async[i].used && comb_async[i].seed) {
            return true;
        }
    }
    return false;
}

/* Flip time: seeded states whose modules are all resident become
 * pipelines; the others wait, never compiled synchronously. */
static void seed_pipe_pump(PGRAPHGLState *r, unsigned max_items)
{
    if (r->seed.state != 1) {
        return;
    }
    bool progress = false, completed_sweep = false;
    unsigned remaining = MIN(max_items, r->seed.pipe_n);
    while (remaining-- && r->seed.pipe_n) {
        unsigned i = r->seed.pipe_cursor;
        if (i >= r->seed.pipe_n) {
            i = 0;
        }
        ShaderState *st = &r->seed.pipe_states[i];
        ShaderModuleCacheKey keys[3];
        bool need_gs = state_module_keys(st, keys);
        bool ready = (!need_gs || module_known(r, &keys[0])) &&
                     module_known(r, &keys[1]) && module_known(r, &keys[2]);
        if (!ready) {
            r->seed.pipe_cursor = i + 1;
            if (r->seed.pipe_cursor == r->seed.pipe_n) {
                r->seed.pipe_cursor = 0;
                completed_sweep = true;
            }
            continue;
        }
        uint64_t h = fast_hash((const uint8_t *)st, sizeof(*st));
        LruNode *node = lru_lookup(&r->shader_cache, h, st);
        ShaderBinding *b = container_of(node, ShaderBinding, node);
        if (!b->initialized) {
            /* Modules resident: pipeline setup only. b->uses stays 0, so a
             * state this session never draws adds nothing to the seed. */
            generate_shaders(r, b);
        }
        r->seed.pipe_states[i] = r->seed.pipe_states[--r->seed.pipe_n];
        r->seed.pipe_cursor = i;
        if (i >= r->seed.pipe_n) {
            r->seed.pipe_cursor = 0;
            completed_sweep = true;
        }
        r->seed.pipes_built++;
        progress = true;
    }
    if (progress && r->shader_binding && r->shader_binding->initialized) {
        /* generate_shaders leaves its own pipeline bound; restore the
         * live one so the not-dirty bind fast path stays truthful. */
        glUseProgram(0);
        glBindProgramPipeline(r->shader_binding->gl_pipeline);
    }
    if (!r->seed.pipe_n && r->seed.mod_idx >= r->seed.mod_n) {
        seed_finish(r, "complete");
    } else if (!progress && completed_sweep &&
               r->seed.mod_idx >= r->seed.mod_n && !seed_jobs_in_flight(r)) {
        /* Leftovers wait on modules that never landed (stale seed, failed
         * compile): dropped after 1800 passes without progress. */
        if (++r->seed.stall_flips > 1800) {
            seed_finish(r, "gave up on leftovers");
        }
    } else if (progress) {
        r->seed.stall_flips = 0;
    }
}

static void comb_async_service(PGRAPHGLState *r, unsigned max_items)
{
    /* The service builds separable stage programs; without separate
     * shader objects nothing can adopt them. */
    if (!gl_sso_enabled()) {
        return;
    }
    unsigned items = 0;

    if (r->seed.state == -1) {
        seed_load(r);
    }

    if (!gen_thr_up) {
        if (!retro_eventcount_init(&gen_ready)) {
            abort();
        }
        gen_thr = sthread_create(shader_gen_worker, NULL);
        if (!gen_thr) {
            abort();
        }
        gen_thr_up = true;
    }

    /* 1) Submit finished sources to GL: cheap per module, the driver's
     * compiler threads take it from there. */
    for (int i = 0; i < GEN_JOBS; i++) {
        if (!gen_jobs[i].used ||
            retro_atomic_load_acquire_int(&gen_jobs[i].state) != GEN_COMPLETE) {
            continue;
        }
        if (items++ >= max_items) {
            break;
        }
        int slot = -1;
        for (unsigned s = 0; s < ARRAY_SIZE(comb_async); s++) {
            if (!comb_async[s].used) {
                slot = s;
                break;
            }
        }
        if (slot < 0) {
            break;
        }
        comb_async_submit(r, i, slot);
    }

    /* 2) Feed the worker: PSH combiners from the dynamic queue... */
    bool fed = false;
    PshState st;
    bool gate;
    while (gen_jobs_have_free(1) && comb_async_take(r, &st, &gate)) {
        if (!gate) {
            /* The draw's exact stage keys: one vertex prefix, optional
             * geometry (including its UI-clamp option), and fragment. The
             * three-slot reservation keeps this owned record pending under
             * pressure. Only this thread changes job-slot ownership. */
            const ShaderState *queued =
                &r->spec_pending[r->spec_pending_r %
                                 ARRAY_SIZE(r->spec_pending)];
            ShaderModuleCacheKey keys[3];
            bool need_gs = state_module_keys(queued, keys);
            bool fragment_cached = module_known(r, &keys[2]);
            for (unsigned stage = need_gs ? 0 : 1; stage < 3; stage++) {
                bool cached = stage == 2 ? fragment_cached :
                                          module_known(r, &keys[stage]);
                if (!cached) {
                    fed |= gen_job_push(&keys[stage], false, false);
                }
            }
            /* No queue view survives this point. Jobs own immutable keys;
             * cached stages retain their existing native owners. */
            r->spec_pending_r++;
            if (fragment_cached) {
                spec_track_adopted(r, &st);
            }
            continue;
        }
        ShaderModuleCacheKey key;
        memset(&key, 0, sizeof(key));
        key.kind = GL_FRAGMENT_SHADER;
        key.psh.state = st;
        /* Gated combiners must reach adoption even if cached. */
        fed |= gen_job_push(&key, gate, false);
    }

    /* ...and dynamic vertex families (both variants). */
    /* Taking a family consumes its owned pending record. Both prefix variants
     * must fit before taking it: losing one leaves adoption short of two and
     * the family keeps specialising on the draw path indefinitely. */
    while (gen_jobs_have_free(2)) {
        VshState vst;
        if (!pgraph_glsl_vsh_dynamic_pending_take(&vst)) {
            break;
        }
        for (int prefix = 0; prefix < 2; prefix++) {
            ShaderModuleCacheKey key;
            memset(&key, 0, sizeof(key));
            key.kind = GL_VERTEX_SHADER;
            key.vsh.state = vst;
            key.vsh.glsl_opts.prefix_outputs = prefix;
            fed |= gen_job_push(&key, false, false);
        }
    }

    /* ...and cold-boot seed keys, lowest priority: only leftover slots,
     * so live compile demand always wins. */
    while (r->seed.state == 1 && r->seed.mod_idx < r->seed.mod_n &&
           gen_jobs_have_free(1)) {
        ShaderModuleCacheKey *sk = &r->seed.mod_keys[r->seed.mod_idx++];
        if (module_known(r, sk)) {
            continue;
        }
        fed |= gen_job_push(sk, false, true);
    }

    if (fed) {
        retro_eventcount_notify(&gen_ready);
    }

    bool has_completion = gl_has_parallel_compile();
    for (unsigned i = 0; i < ARRAY_SIZE(comb_async); i++) {
        if (!comb_async[i].used) {
            continue;
        }
        GLint done = GL_TRUE;
        if (has_completion) {
            glGetProgramiv(comb_async[i].gl_program,
                           GL_COMPLETION_STATUS_ARB, &done);
        }
        if (!done) {
            continue;
        }
        GLint ok = 0;
        glGetProgramiv(comb_async[i].gl_program, GL_LINK_STATUS, &ok);
        glDetachShader(comb_async[i].gl_program, comb_async[i].gl_shader);
        glDeleteShader(comb_async[i].gl_shader);
        if (ok) {
            r->comb_adopt_hash = fast_hash((void *)&comb_async[i].key,
                                           sizeof(ShaderModuleCacheKey));
            r->comb_adopt_program = comb_async[i].gl_program;
            get_shader_module_for_key(r, &comb_async[i].key);
            if (r->comb_adopt_program) {
                delete_program(r, r->comb_adopt_program);
                r->comb_adopt_program = 0;
            }
            comb_async_adopted(r, &comb_async[i].key, comb_async[i].gate,
                               comb_async[i].seed);
        } else {
            GLchar log[1024];
            glGetProgramInfoLog(comb_async[i].gl_program, sizeof(log),
                                NULL, log);
            fprintf(stderr, "nv2a: dynamic combiner link failed: %s\n", log);
            delete_program(r, comb_async[i].gl_program);
        }
        comb_async[i].used = false;
    }
}

/* Boot prewarm: the on-disk module cache is loaded into GL in idle flips,
 * so no first draw pays a glProgramBinary. Each file holds its full key,
 * so the list is just the directory. */
static void module_prewarm_start(PGRAPHGLState *r)
{
    r->prewarm.state = 0;
    if (!g_config.perf.cache_shaders || !gl_sso_enabled()) {
        return;
    }
    r->prewarm.base = g_strdup_printf("%s/shader_modules",
                                     xemu_settings_get_base_path());
    r->prewarm.root = g_dir_open(r->prewarm.base, 0, NULL);
    if (!r->prewarm.root) {
        module_prewarm_finish(r);
        return;
    }
    r->prewarm.count = 0;
    r->prewarm.state = 1;
}

/* Read just the header of a cached module file to recover its key.
 * Same layout/checks as module_load_from_disk. */
static bool module_file_read_key(const char *path, ShaderModuleCacheKey *key)
{
    XemuFileMap m;
    if (!xemu_file_map(&m, path)) {
        return false;
    }
    FileCursor c = { m.data, m.len };
    bool ok = module_read_header(&c, key);
    xemu_file_unmap(&m);
    return ok;
}

static void module_prewarm_pump(PGRAPHGLState *r, unsigned max_items)
{
    if (!max_items) {
        return;
    }
    if (r->prewarm.state == -1) {
        module_prewarm_start(r);
    }
    if (r->prewarm.state != 1) {
        return;
    }
    /* Every directory advance consumes the existing per-flip quota, even
     * for empty/unreadable buckets. Never enumerate the whole cache here.
     * GDir owns each returned name only until its next read/close: build
     * the owned bucket/file path immediately, before advancing again. */
    for (unsigned items = 0; items < max_items; items++) {
        if (!r->prewarm.bucket) {
            const char *sub = g_dir_read_name(r->prewarm.root);
            if (!sub) {
                fprintf(stderr, "nv2a: shader module prewarm complete (%u files)\n",
                        r->prewarm.count);
                module_prewarm_finish(r);
                return;
            }
            r->prewarm.bucket_path = g_strdup_printf("%s/%s", r->prewarm.base, sub);
            r->prewarm.bucket = g_dir_open(r->prewarm.bucket_path, 0, NULL);
            if (!r->prewarm.bucket) {
                g_free(r->prewarm.bucket_path);
                r->prewarm.bucket_path = NULL;
            }
            continue;
        }
        const char *fn = g_dir_read_name(r->prewarm.bucket);
        if (!fn) {
            g_dir_close(r->prewarm.bucket);
            r->prewarm.bucket = NULL;
            g_free(r->prewarm.bucket_path);
            r->prewarm.bucket_path = NULL;
            continue;
        }
        char *path = g_strdup_printf("%s/%s", r->prewarm.bucket_path, fn);
        r->prewarm.count++;
        ShaderModuleCacheKey key;
        if (module_file_read_key(path, &key)) {
            /* Init of a fresh node loads the binary from disk and
             * resolves uniform locations, off the draw path. */
            get_shader_module_for_key(r, &key);
        }
        g_free(path);
    }
}

/* One point sampling each fresh texture makes the driver finalize it here,
 * at the flip, not in the first frame that samples it. Host plumbing:
 * every write is masked off. */
static void touch_fresh_textures(PGRAPHGLState *r, unsigned max_items)
{
    if (r->fresh_tex_r == r->fresh_tex_w) {
        return;
    }
    if (r->fresh_tex_w - r->fresh_tex_r > ARRAY_SIZE(r->fresh_tex)) {
        r->fresh_tex_r = r->fresh_tex_w - ARRAY_SIZE(r->fresh_tex);
    }
    unsigned items = 0;

    if (!r->touch_prog) {
        r->touch_prog = pgraph_gl_compile_shader(
            "#version 400 core\n"
            "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n",
            "#version 400 core\n"
            "uniform sampler2D s;\n"
            "out vec4 o;\n"
            "void main() { o = texture(s, vec2(0.5)); }\n");
        glUseProgram(r->touch_prog);
        glUniform1i(glGetUniformLocation(r->touch_prog, "s"), 0);
        glUseProgram(0);
    }

    GLint prev_vao = 0, prev_fbo = 0;
    GLint prev_tex0 = 0, prev_active = 0, sbox[4];
    GLboolean cmask[4], dmask;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex0);
    glGetBooleanv(GL_COLOR_WRITEMASK, cmask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &dmask);
    GLint smask = 0;
    glGetIntegerv(GL_STENCIL_WRITEMASK, &smask);
    GLboolean scissor_was = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, sbox);

    if (!r->touch_vao) {
        glGenVertexArrays(1, &r->touch_vao);
    }
    glBindProgramPipeline(0);
    glUseProgram(r->touch_prog);
    glBindVertexArray(r->touch_vao);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glStencilMask(0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 1, 1);

    while (r->fresh_tex_r != r->fresh_tex_w) {
        if (items++ >= max_items) {
            break;
        }
        GLuint tex = r->fresh_tex[r->fresh_tex_r % ARRAY_SIZE(r->fresh_tex)];
        r->fresh_tex_r++;
        if (sampled_texture_contains(&r->sampled_texture_pool, tex) ||
            !glIsTexture(tex)) {
            continue; /* destroyed or parked since queueing */
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glDrawArrays(GL_POINTS, 0, 1);
    }
    (void)glGetError();

    glBindTexture(GL_TEXTURE_2D, prev_tex0);
    glActiveTexture(prev_active);
    glBindVertexArray(prev_vao);
    /* Back to the live binding the bind fast path assumes, not read back:
     * Intel's Windows driver 21.20.16.4526 reports the bound pipeline's
     * name as GL_CURRENT_PROGRAM. */
    ShaderBinding *live =
        (r->shader_binding && r->shader_binding->initialized) ?
            r->shader_binding : NULL;
    if (gl_sso_enabled()) {
        glUseProgram(0);
        glBindProgramPipeline(live ? live->gl_pipeline : 0);
    } else {
        glUseProgram(live ? live->gl_program : 0);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_fbo);
    glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
    glDepthMask(dmask);
    glStencilMask(smask);
    if (!scissor_was) {
        glDisable(GL_SCISSOR_TEST);
    }
    glScissor(sbox[0], sbox[1], sbox[2], sbox[3]);
}

/* Fixed per-flip work quotas keep maintenance independent of host time.
 * Driver completion is polled; unfinished work remains queued. */
void pgraph_gl_shaders_flip_service(NV2AState *d)
{
    PGRAPHGLState *r = d->pgraph.gl_renderer_state;
    shader_adopt_disk_results(r, 64);
    pgraph_gl_tex_shadow_reset(r);
    touch_fresh_textures(r, 4);
    comb_async_service(r, 2);
    module_save_queue_drain(r, 1);
    module_prewarm_pump(r, 1);
    seed_pipe_pump(r, 4);
    spec_binding_pump(r);
}

void pgraph_gl_bind_shaders(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    shader_adopt_disk_results(r, 64);

    bool binding_changed = false;
    if (r->shader_binding &&
        r->shader_binding->dyn_gen == pgraph_glsl_dynamic_gen &&
        r->shader_binding->state.geom.wide_lines == r->wide_lines &&
        !pgraph_glsl_check_shader_state_dirty(pg, &r->shader_binding->state)) {
        nv2a_profile_inc_counter(NV2A_PROF_SHADER_BIND_NOTDIRTY);
        goto update_uniforms;
    }

    ShaderBinding *old_binding = r->shader_binding;
    /* Preserve an exact reservation until flip-time completion; otherwise
     * later draws could continually displace it and remain on covers. */
    if (r->spec_binding && spec_binding_reservation(r) < 0) {
        spec_binding_cancel(r);
    }
    ShaderState state = pgraph_glsl_get_shader_state(pg);
    state.geom.wide_lines = r->wide_lines;

    NV2A_GL_DGROUP_BEGIN("%s (%s)", __func__,
                         state.vsh.is_fixed_function ? "FF" : "PROG");

    uint64_t shader_state_hash =
        fast_hash((uint8_t *)&state, sizeof(ShaderState));

    LruNode *node = lru_lookup(&r->shader_cache, shader_state_hash, &state);
    ShaderBinding *binding = container_of(node, ShaderBinding, node);

    if (!binding->initialized && gl_sso_enabled() && first_meet_enabled()) {
        uint64_t ph =
            fast_hash((const uint8_t *)&state.psh, sizeof(state.psh));
        int t = spec_track_find(r, ph);
        if (t >= 0 &&
            (r->spec_track[t].phase == SPEC_ADOPTED ||
             r->spec_track[t].phase == SPEC_PIPE_RESERVED) &&
            state_modules_known(r, &state)) {
            /* Prepared stages alone are not a pipeline. Keep an exact-key
             * ready cover drawing while flip-time setup finishes this one. */
            ShaderBinding *cover = state_modules_ready(r, &state) ?
                                    ready_cover_binding(r, &state) : NULL;
            if (cover && spec_binding_track(r, binding)) {
                r->spec_track[t].phase = SPEC_PIPE_RESERVED;
                /* Keep ADOPTED until flip-time setup completes: unchanged
                 * draws must not enqueue another owned recipe snapshot. */
                binding = cover;
                nv2a_profile_inc_counter(NV2A_PROF_SHADER_FALLBACK);
            } else {
                /* A different recipe cannot displace the reservation. Keep
                 * a shared fragment marker until that owner finishes. */
                ShaderBinding *pending = r->spec_binding;
                if (!pending || fast_hash((const uint8_t *)&pending->state.psh,
                                           sizeof(pending->state.psh)) != ph) {
                    r->spec_track[t].phase = 0;
                }
            }
        } else {
            /* Project only our unpublished PSH metadata. Restore it before
             * the exact recipe can be handed to a queue; VSH and geometry
             * remain in the original capture rather than a full-state clone. */
            PshState exact_psh;
            memcpy(&exact_psh, &state.psh, sizeof(exact_psh));
            if (pgraph_glsl_psh_canonicalize_dynamic(&state.psh)) {
                uint64_t fh = fast_hash((const uint8_t *)&state, sizeof(state));
                LruNode *fn = lru_lookup(&r->shader_cache, fh, &state);
                ShaderBinding *fbind = container_of(fn, ShaderBinding, node);
                if (!fbind->initialized) {
                    /* The sibling is interpreter code, never worth a sync
                     * stall: queue it, and take the normal path meanwhile. */
                    uint64_t sh = fast_hash((const uint8_t *)&state.psh,
                                            sizeof(state.psh));
                    int ts = spec_track_find(r, sh);
                    if (ts >= 0 && r->spec_track[ts].phase == SPEC_ADOPTED &&
                        state_modules_known(r, &state)) {
                        r->spec_track[ts].phase = 0;
                        generate_shaders(r, fbind); /* modules hit: cheap */
                    } else if (ts < 0) {
                        spec_queue(r, sh, &state);
                    }
                    if (!fbind->initialized) {
                        /* Keep the queued cover's native owner available to
                         * the flip service while the exact path draws. */
                        spec_binding_track(r, fbind);
                    }
                }
                memcpy(&state.psh, &exact_psh, sizeof(exact_psh));
                if (fbind->initialized) {
                    if (t < 0) {
                        spec_queue(r, ph, &state);
                    }
                    nv2a_profile_inc_counter(NV2A_PROF_SHADER_FALLBACK);
                    if (binding != fbind && !binding->initialized) {
                        spec_binding_track(r, binding);
                    }
                    binding = fbind;
                }
            } else {
                memcpy(&state.psh, &exact_psh, sizeof(exact_psh));
            }
        }
    }

    if (!binding->initialized &&
        (gl_sso_enabled() || !pgraph_gl_shader_load_from_memory(binding))) {
        nv2a_profile_inc_counter(NV2A_PROF_SHADER_GEN);
        generate_shaders(r, binding);
    }
    if (g_config.perf.cache_shaders && !gl_sso_enabled()) {
        pgraph_gl_shader_cache_to_disk(r, binding);
    }
    assert(binding->initialized);
    binding->uses++;
    r->shader_binding = binding;
    binding->dyn_gen = pgraph_glsl_dynamic_gen;
    pg->program_data_dirty = false;

    /* Dirty now means written since this build, as the map's only reader,
     * check_shader_state_dirty, expects (VK clears the map on its own). */
    pgraph_clear_dirty_reg_map(pg);

    binding_changed = (r->shader_binding != old_binding);
    if (binding_changed) {
        nv2a_profile_inc_counter(NV2A_PROF_SHADER_BIND);
        if (gl_sso_enabled()) {
            /* A bound program takes priority over a pipeline object. */
            glUseProgram(0);
            glBindProgramPipeline(r->shader_binding->gl_pipeline);
        } else {
            glUseProgram(r->shader_binding->gl_program);
        }
    }

    NV2A_GL_DGROUP_END();

update_uniforms:
    assert(r->shader_binding);
    assert(r->shader_binding->initialized);
    update_shader_uniforms(pg, r->shader_binding);
}

GLuint pgraph_gl_compile_shader(const char *vs_src, const char *fs_src)
{
    GLint status;
    char err_buf[512];

    // Compile vertex shader
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        glGetShaderInfoLog(vs, sizeof(err_buf), NULL, err_buf);
        err_buf[sizeof(err_buf)-1] = '\0';
        fprintf(stderr, "Vertex shader compilation failed: %s\n", err_buf);
        exit(1);
    }

    // Compile fragment shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        glGetShaderInfoLog(fs, sizeof(err_buf), NULL, err_buf);
        err_buf[sizeof(err_buf)-1] = '\0';
        fprintf(stderr, "Fragment shader compilation failed: %s\n", err_buf);
        exit(1);
    }

    // Link vertex and fragment shaders
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);

    // Flag shaders for deletion (will still be retained for lifetime of prog)
    glDeleteShader(vs);
    glDeleteShader(fs);

    return prog;
}

/* Every Vulkan shader of the shipped seed dictionary (data/shader_seeds.bin:
 * the states the games are known to meet) through the core's GLSL to
 * SPIR-V compiler: the GLSL the generators write for each state, as the
 * renderer asks for it, both with and without push constants for the
 * uniform attributes. A construct a generator writes that the compiler
 * does not take fails here, not a game's boot. Linked with the core's own
 * objects (Makefile: seedshaders), so it reads the states with the core's
 * own generators.
 *
 *   seedshaders <shader_seeds.bin> [out dir]
 *
 * With an out dir, each distinct module is written there as NNNNN_<stage>.spv
 * with its .glsl, for spirv-val or a comparison with another compiler.
 * Exit 1 on any failure. Plain C11 (the core's headers want it).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include <encodings/deflate.h>
#include <stdio.h>

/* The dictionary and seed file layouts (hw/xbox/nv2a/pgraph/seed.c) */
typedef struct { uint32_t magic, version, ngames; } DictHeader;
typedef struct { char game[32]; uint32_t raw_len, z_len; } DictEntry;
typedef struct { uint32_t magic, version, nsections, reserved; } SeedHeader;
typedef struct { uint32_t id, itemsize, count, reserved; } SeedSection;
#define SEED_DICT_MAGIC 0x44445358u
#define SEED_MAGIC 0x44455358u
#define SECTION_VK_STATES 3

static int fails, total, unique;
static const char *out_dir;
static GHashTable *seen;

static void compile(const char *game, GlslcStage stage, MString *code,
                    const char *what)
{
    GlslcOptions opts = { .spirv_1_6 = true };
    GlslcResult r;
    const char *src = mstring_get_str(code);
    total++;
    if (!glslc_compile(stage, src, &opts, &r)) {
        fails++;
        printf("FAIL %s %s line %d: %s\n", game, what, r.error_line, r.error);
        if (out_dir) {
            char *name = g_strdup_printf("%s/fail_%d.glsl", out_dir, fails);
            FILE *f = fopen(name, "w");
            if (f) {
                fputs(src, f);
                fclose(f);
            }
            g_free(name);
        }
    } else {
        if (out_dir) {
            char *key = g_compute_checksum_for_string(G_CHECKSUM_SHA1, src, -1);
            if (!g_hash_table_contains(seen, key)) {
                g_hash_table_add(seen, key);
                unique++;
                char *name = g_strdup_printf("%s/%05d_%s.spv", out_dir, unique,
                                             what);
                FILE *f = fopen(name, "wb");
                if (f) {
                    fwrite(r.words, 4, r.num_words, f);
                    fclose(f);
                }
                g_free(name);
                name = g_strdup_printf("%s/%05d_%s.glsl", out_dir, unique,
                                       what);
                f = fopen(name, "w");
                if (f) {
                    fputs(src, f);
                    fclose(f);
                }
                g_free(name);
            } else {
                g_free(key);
            }
        }
        free(r.words);
    }
    mstring_unref(code);
}

/* The generators ask whether the OpenGL renderer's tuning is on (the
 * vertex and pixel shaders differ with it): both answers are compiled. */
static bool boost_gl;
bool pgraph_gpu_boost_gl(void);
bool pgraph_gpu_boost_gl(void)
{
    return boost_gl;
}

/* The modules of one state, as pgraph_vk_ref_state_modules asks for them
 * (hw/xbox/nv2a/pgraph/vk/shaders.c, vk_state_module_keys) */
static void compile_state_variant(const char *game, const ShaderState *st)
{
    bool need_gs = pgraph_glsl_need_geom(&st->geom);
    if (need_gs) {
        GenGeomGlslOptions g = { .vulkan = true };
        compile(game, GLSLC_STAGE_GEOMETRY, pgraph_glsl_gen_geom(&st->geom, g),
                "geom");
    }
    for (int pc = 0; pc < 2; pc++) {
        GenVshGlslOptions v = { .vulkan = true,
                                .prefix_outputs = need_gs,
                                .use_push_constants_for_uniform_attrs = pc,
                                .ubo_binding = 0 };
        compile(game, GLSLC_STAGE_VERTEX, pgraph_glsl_gen_vsh(&st->vsh, v),
                pc ? "vsh-pc" : "vsh");
    }
    GenPshGlslOptions p = { .vulkan = true, .ubo_binding = 1, .tex_binding = 2 };
    compile(game, GLSLC_STAGE_FRAGMENT, pgraph_glsl_gen_psh(&st->psh, p),
            "psh");
}

static void compile_state(const char *game, const ShaderState *st)
{
    boost_gl = false;
    compile_state_variant(game, st);
    boost_gl = true;
    compile_state_variant(game, st);
}

static void compile_seed(const char *game, const uint8_t *raw, size_t len)
{
    SeedHeader sh;
    if (len < sizeof(sh)) {
        return;
    }
    memcpy(&sh, raw, sizeof(sh));
    if (sh.magic != SEED_MAGIC) {
        printf("%s: not a seed file\n", game);
        fails++;
        return;
    }
    size_t off = sizeof(sh);
    unsigned states = 0;
    for (uint32_t s = 0; s < sh.nsections && off + sizeof(SeedSection) <= len;
         s++) {
        SeedSection sec;
        memcpy(&sec, raw + off, sizeof(sec));
        off += sizeof(sec);
        size_t bytes = (size_t)sec.itemsize * sec.count;
        if (bytes > len - off) {
            break;
        }
        if (sec.id == SECTION_VK_STATES) {
            if (sec.itemsize != sizeof(ShaderState)) {
                printf("%s: states of %u bytes, the core's are %zu: the "
                       "dictionary needs regenerating\n",
                       game, sec.itemsize, sizeof(ShaderState));
                fails++;
            } else {
                for (uint32_t k = 0; k < sec.count; k++) {
                    ShaderState st;
                    memcpy(&st, raw + off + k * sizeof(st), sizeof(st));
                    compile_state(game, &st);
                    states++;
                }
            }
        }
        off += bytes;
    }
    printf("%s: %u states\n", game, states);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: seedshaders <shader_seeds.bin> [out dir]\n");
        return 2;
    }
    out_dir = argc > 2 ? argv[2] : NULL;
    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gchar *dict;
    gsize len;
    if (!g_file_get_contents(argv[1], &dict, &len, NULL)) {
        fprintf(stderr, "seedshaders: cannot read %s\n", argv[1]);
        return 2;
    }
    const uint8_t *d = (const uint8_t *)dict;
    DictHeader h;
    if (len < sizeof(h)) {
        return 2;
    }
    memcpy(&h, d, sizeof(h));
    if (h.magic != SEED_DICT_MAGIC ||
        h.ngames > (len - sizeof(h)) / sizeof(DictEntry)) {
        fprintf(stderr, "seedshaders: not a seed dictionary\n");
        return 2;
    }
    size_t off = sizeof(h) + h.ngames * sizeof(DictEntry);
    for (uint32_t i = 0; i < h.ngames; i++) {
        DictEntry e;
        memcpy(&e, d + sizeof(h) + i * sizeof(e), sizeof(e));
        if (e.z_len > len - off || !e.raw_len || e.raw_len > (64u << 20)) {
            fprintf(stderr, "seedshaders: a bad entry\n");
            return 2;
        }
        uint8_t *raw = g_malloc(e.raw_len);
        size_t dlen = 0;
        void *inf = rinflate_new(15);
        rinflate_set_in(inf, d + off, e.z_len);
        rinflate_set_out(inf, raw, e.raw_len);
        int rc = rinflate_process(inf, NULL, &dlen);
        rinflate_free(inf);
        off += e.z_len;
        if (rc != RDEFLATE_PROCESS_END || dlen != e.raw_len) {
            printf("%.32s: inflate failed\n", e.game);
            fails++;
        } else {
            compile_seed(e.game, raw, dlen);
        }
        g_free(raw);
    }
    g_free(dict);
    printf("%d shaders, %d distinct, %d failed\n", total, unique, fails);
    return fails != 0 || total == 0;
}

/* What the generators' objects reference and never reach from here */
void pgraph_get_inline_values(PGRAPHState *pg, uint16_t attrs,
                              float values[NV2A_VERTEXSHADER_ATTRIBUTES][4],
                              int *count);
void pgraph_get_inline_values(PGRAPHState *pg, uint16_t attrs,
                              float values[NV2A_VERTEXSHADER_ATTRIBUTES][4],
                              int *count)
{
    *count = 0;
}

struct NV2AState;
void *nv_dma_map(struct NV2AState *d, uint64_t dma_obj_address, uint64_t *len);
void *nv_dma_map(struct NV2AState *d, uint64_t dma_obj_address, uint64_t *len)
{
    return NULL;
}

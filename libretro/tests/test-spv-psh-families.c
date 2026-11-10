/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Eligibility is compared with the independent production GLSL generator. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "hw/xbox/nv2a/nv2a_regs.h"

enum FamilyFlags {
    BUMP = 1, DEPENDENT = 2, CUBE = 4, PREVIOUS_DOT = 8,
    THREE_DOTS = 16, ONLY_2D = 32, PASSTHRU = 64,
};
static const struct { unsigned mode, unit, flags; } families[] = {
    {PS_TEXTUREMODES_PROJECT2D, 0, 0},
    {PS_TEXTUREMODES_PROJECT3D, 0, 0},
    {PS_TEXTUREMODES_CUBEMAP, 0, CUBE},
    {PS_TEXTUREMODES_PASSTHRU, 0, PASSTHRU},
    {PS_TEXTUREMODES_CLIPPLANE, 0, 0},
    {PS_TEXTUREMODES_BUMPENVMAP, 1, BUMP},
    {PS_TEXTUREMODES_BUMPENVMAP_LUM, 1, BUMP},
    {PS_TEXTUREMODES_DOT_ST, 2, PREVIOUS_DOT | ONLY_2D},
    {PS_TEXTUREMODES_DOT_ZW, 2, PREVIOUS_DOT},
    {PS_TEXTUREMODES_DOT_RFLCT_DIFF, 2, PREVIOUS_DOT | CUBE},
    {PS_TEXTUREMODES_DOT_RFLCT_SPEC, 3, PREVIOUS_DOT | THREE_DOTS | CUBE},
    {PS_TEXTUREMODES_DOT_STR_3D, 3, PREVIOUS_DOT | THREE_DOTS},
    {PS_TEXTUREMODES_DOT_STR_CUBE, 3, PREVIOUS_DOT | THREE_DOTS | CUBE},
    {PS_TEXTUREMODES_DPNDNT_AR, 1, DEPENDENT},
    {PS_TEXTUREMODES_DPNDNT_GB, 1, DEPENDENT},
    {PS_TEXTUREMODES_DOTPRODUCT, 1, 0},
    {PS_TEXTUREMODES_NONE, 0, 0},
};
static void release(GlslcResult *r)
{
    glslc_result_free_words(r);
    free(r->uniforms.members);
    free(r->push_constants.members);
}
static void write_module(const char *directory, const char *kind,
                         unsigned number, const GlslcResult *r)
{
    char path[4096];
    int n = snprintf(path, sizeof(path), "%s/%s-%u.spv", directory, kind, number);
    assert(n > 0 && (size_t)n < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(r->words, 4, r->num_words, file) == r->num_words);
    assert(!fclose(file));
}
int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    unsigned selected = argc == 3 ? (unsigned)strtoul(argv[2], NULL, 10) : UINT32_MAX;
    unsigned total = 0, compiled_count = 0, native_count = 0, asserted = 0;
    const unsigned shadow_flags[] = {0, 1, 3, 31};
    const unsigned functions[] = {SHADOW_DEPTH_FUNC_LESS,
                                 SHADOW_DEPTH_FUNC_NEVER, SHADOW_DEPTH_FUNC_ALWAYS};
    for (unsigned family = 0; family < sizeof(families) / sizeof(families[0]); family++) {
        for (unsigned f = 0; f < 384; f++) {
            unsigned number = family * 384 + f;
            if (selected != UINT32_MAX && selected != number) {
                continue;
            }
            unsigned mode = families[family].mode, unit = families[family].unit;
            unsigned flags = families[family].flags, reg = PS_REGISTER_T0 + unit;
            PshState s = {.depth_format = DEPTH_FORMAT_D24};
            for (unsigned j = 0; j < 4; j++) {
                s.dim_tex[j] = 2;
            }
            s.shader_stage_program = mode << (unit * 5);
            if (unit) {
                s.shader_stage_program |= PS_TEXTUREMODES_PASSTHRU;
            }
            if (flags & PREVIOUS_DOT) {
                s.shader_stage_program |= PS_TEXTUREMODES_DOTPRODUCT << 5;
            }
            if (flags & THREE_DOTS) {
                s.shader_stage_program |= PS_TEXTUREMODES_DOTPRODUCT << 10;
            }
            s.final_inputs_0 = 0x20000000u | (reg << 16);
            s.final_inputs_1 = (reg | 16u) << 8;
            /* Five independent shape bits, four shadow formats, three comparisons. */
            s.dim_tex[unit] = 2 + (f & 1);
            s.rect_tex[unit] = !!(f & 2);
            s.border_adjust[unit] = !!(f & 4);
            s.tex_cubemap[unit] = !!(f & 8);
            s.tex_x8y24[unit] = !!(f & 16);
            s.shadow_map[unit] = shadow_flags[(f / 32) % 4];
            s.shadow_depth_func = functions[f / 128];
            /* These are asserted reference preconditions, not native assumptions.
             * Selected witness subprocesses deliberately execute the assertions. */
            bool invalid = ((flags & (CUBE | DEPENDENT | ONLY_2D)) && s.dim_tex[unit] != 2) ||
                ((flags & DEPENDENT) && s.rect_tex[unit]) ||
                ((flags & PASSTHRU) && s.border_adjust[unit]) ||
                ((flags & (CUBE | DEPENDENT | BUMP | ONLY_2D)) && s.shadow_map[unit]);
            if (invalid && selected == UINT32_MAX) {
                asserted++;
                continue;
            }
            total++;
            MString *text = pgraph_glsl_gen_psh(&s, (GenPshGlslOptions){
                .vulkan = true, .locations = true, .ubo_binding = 1, .tex_binding = 2,
            });
            GlslcResult reference;
            bool compiled = glslc_compile(GLSLC_STAGE_FRAGMENT, mstring_get_str(text),
                &(GlslcOptions){.emit_layout = true, .spirv_1_6 = true}, &reference);
            bool native = pgraph_spv_psh_supported(&s, (SpvPshOptions){
                .ubo_binding = 1, .tex_binding = 2, .spirv_1_6 = true,
            });
#ifdef NATIVE_FAMILY_GAP
            if (mode == PS_TEXTUREMODES_PROJECT3D) {
                native = false;
            }
#endif
            if (compiled != native) {
                fprintf(stderr, "native eligibility mismatch: mode=%u case=%u compiled=%u native=%u\n",
                        mode, number, compiled, native);
                if (compiled) {
                    release(&reference);
                }
                mstring_unref(text);
                return 1;
            }
            compiled_count += compiled;
            native_count += native;
            if (compiled) {
                GlslcResult direct;
                assert(pgraph_spv_psh(&s, (SpvPshOptions){
                    .ubo_binding = 1, .tex_binding = 2, .spirv_1_6 = true,
                }, &direct));
                write_module(argv[1], "reference", number, &reference);
                write_module(argv[1], "native", number, &direct);
                release(&direct);
                release(&reference);
            }
            mstring_unref(text);
        }
    }
    printf("Family eligibility: %u candidates, %u reference-compiled/native pairs, "
           "%u reference assertion preconditions\n", total, compiled_count, asserted);
    return !(total == 3360 && compiled_count == 2880 && native_count == 2880 && asserted == 3168);
}

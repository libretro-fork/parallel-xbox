/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Static register-source/output eligibility against the production reference.
 * This is a bounded graph audit, not proof of arbitrary register sequences. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <spirv-tools/libspirv.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
static void release(GlslcResult *r)
{
    glslc_result_free_words(r);
    free(r->uniforms.members);
    free(r->push_constants.members);
}
int main(int argc, char **argv)
{
    unsigned tested = 0, mismatches = 0, pairs = 0;
    assert(argc == 2);
    (void)argv;
    spv_context validator = spvContextCreate(SPV_ENV_VULKAN_1_3);
    assert(validator);
    /* Source bytes cover every mapping/channel at each general/final input.
     * Registers 6/7 are reserved; EF is only defined after E/F evaluation.
     * Output cells cross all eight operation/scale/blue-to-alpha flag bits,
     * each legal destination and each AB/CD/SUM RGB/alpha destination slot. */
    for (unsigned category = 0; category < 2; category++) {
        for (unsigned position = 0; position < (category ? 6 : 15); position++)
            for (unsigned byte = 0; byte < (category ? 9 * 256 : 256); byte++) {
                unsigned reg = byte & 15;
                if (!category &&
                    (reg == 6 || reg == 7 ||
                     (reg == 15 &&
                      (position < 8 || position == 12 || position == 13))))
                    continue;
                PshState s = { 0 };
                s.combiner_control = 1;
                s.final_inputs_0 = 0x04000000;
                s.final_inputs_1 = 0x00000400;
                if (category) {
                    unsigned dests[] = { 0, 4, 5, 8, 9, 10, 11, 12, 13 };
                    unsigned dest = dests[byte % 9];
                    unsigned out =
                        (dest << ((position % 3) * 4)) | ((byte / 9) << 12);
                    if (position < 3)
                        s.rgb_outputs[0] = out;
                    else
                        s.alpha_outputs[0] = out;
                } else if (position < 4)
                    s.rgb_inputs[0] = byte << (24 - position * 8);
                else if (position < 8)
                    s.alpha_inputs[0] = byte << (24 - (position - 4) * 8);
                else if (position < 12)
                    s.final_inputs_0 = (s.final_inputs_0 &
                                        ~(255u << (24 - (position - 8) * 8))) |
                                       (byte << (24 - (position - 8) * 8));
                else
                    s.final_inputs_1 = (s.final_inputs_1 &
                                        ~(255u << (24 - (position - 12) * 8))) |
                                       (byte << (24 - (position - 12) * 8));
                MString *text = pgraph_glsl_gen_psh(
                    &s, (GenPshGlslOptions){ .vulkan = true,
                                             .locations = true,
                                             .ubo_binding = 1,
                                             .tex_binding = 2 });
                GlslcResult r;
                bool ref = glslc_compile(
                    GLSLC_STAGE_FRAGMENT, mstring_get_str(text),
                    &(GlslcOptions){ .emit_layout = true, .spirv_1_6 = true },
                    &r);
                bool native = pgraph_spv_psh_supported(
                    &s, (SpvPshOptions){ .ubo_binding = 1,
                                         .tex_binding = 2,
                                         .spirv_1_6 = true });
#ifdef NATIVE_FAMILY_GAP
                if (position == 0 && byte == 4 && !category)
                    native = false;
#endif
                if (ref != native) {
                    fprintf(stderr,
                            "MISMATCH category=%u position=%u byte=%u "
                            "reference=%u native=%u\n",
                            category, position, byte, ref, native);
                    if (ref) {
                        release(&r);
                    }
                    mstring_unref(text);
                    spvContextDestroy(validator);
                    return 1;
                }
                if (ref && native) {
                    GlslcResult direct;
                    assert(pgraph_spv_psh(&s,
                                          (SpvPshOptions){ .ubo_binding = 1,
                                                           .tex_binding = 2,
                                                           .spirv_1_6 = true },
                                          &direct));
#ifdef BROKEN_SPIRV_CONTROL
                    direct.words[0] = 0;
#endif
                    /* Both result owners survive this synchronous lend. */
                    const GlslcResult *modules[] = { &r, &direct };
                    for (unsigned m = 0; m < 2; m++) {
                        spv_diagnostic diagnostic = NULL;
                        spv_result_t status = spvValidateBinary(
                            validator, modules[m]->words, modules[m]->num_words,
                            &diagnostic);
                        if (status != SPV_SUCCESS) {
                            fprintf(stderr, "module validation failure: %s\n",
                                    diagnostic ? diagnostic->error :
                                                 "no diagnostic");
                            spvDiagnosticDestroy(diagnostic);
                            release(&direct);
                            release(&r);
                            mstring_unref(text);
                            spvContextDestroy(validator);
                            return 1;
                        }
                        spvDiagnosticDestroy(diagnostic);
                    }
                    release(&direct);
                    pairs++;
                }
                if (ref) {
                    release(&r);
                }
                mstring_unref(text);
                tested++;
            }
    }
    spvContextDestroy(validator);
    printf("Combiner audit: %u states, %u pairs, %u mismatches; %u modules "
           "validated in-process\n",
           tested, pairs, mismatches, 2 * pairs);
    assert(tested == 17024 && pairs == 15232);
    return mismatches != 0;
}

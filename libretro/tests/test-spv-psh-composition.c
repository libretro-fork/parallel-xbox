/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Four active-stage composition against production GLSL.
 * This finite mode/profile product does not cover every dependent-texture graph. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <spirv-tools/libspirv.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "spv-psh-composition.h"
static void release(GlslcResult *r)
{
    glslc_result_free_words(r);
    free(r->uniforms.members);
    free(r->push_constants.members);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    (void)argv;
    unsigned tested = 0, pairs = 0;
    spv_context validator = spvContextCreate(SPV_ENV_VULKAN_1_3);
    assert(validator);
    for (unsigned profile = 0; profile < 24; profile++) {
        for (unsigned choice = 0; choice < 1296; choice++) {
            PshState s;
            composition_state(&s, choice, profile);
            MString *text = pgraph_glsl_gen_psh(
                &s, (GenPshGlslOptions){ .vulkan = true,
                                         .locations = true,
                                         .ubo_binding = 1,
                                         .tex_binding = 2 });
            GlslcResult r;
            bool ref = glslc_compile(
                GLSLC_STAGE_FRAGMENT, mstring_get_str(text),
                &(GlslcOptions){ .emit_layout = true, .spirv_1_6 = true }, &r);
            bool native = pgraph_spv_psh_supported(
                &s, (SpvPshOptions){ .ubo_binding = 1,
                                     .tex_binding = 2,
                                     .spirv_1_6 = true });
#ifdef NATIVE_FAMILY_GAP
            if (choice == 100 && profile == 0)
                native = false;
#endif
            if (ref != native) {
                fprintf(stderr,
                        "COMPOSITION_MISMATCH choice=%u profile=%u "
                        "reference=%u native=%u\n",
                        choice, profile, ref, native);
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
                    spv_result_t status =
                        spvValidateBinary(validator, modules[m]->words,
                                          modules[m]->num_words, &diagnostic);
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
    printf("Composition audit: %u states, %u pairs; %u modules validated "
           "in-process\n",
           tested, pairs, 2 * pairs);
    assert(tested == 31104 && pairs == 31104);
    return 0;
}

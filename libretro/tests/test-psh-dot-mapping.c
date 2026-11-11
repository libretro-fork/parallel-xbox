/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
#include <spirv-tools/libspirv.h>
#include "hw/xbox/nv2a/pgraph/spv/psh.h"
static unsigned allocations;
void *psh_test_allocate(size_t n)
{
    allocations++;
    return malloc(n);
}
int main(void)
{
    spv_context validator = spvContextCreate(SPV_ENV_VULKAN_1_3);
    assert(validator);
    const unsigned maps[] = { 8, 16, 24, 32, 48 };
    unsigned cases = 0;
    for (unsigned version = 0; version < 2; version++)
        for (unsigned kind = 0; kind < 3; kind++)
            for (unsigned m = 0; m < 5; m++) {
                PshState s = { .combiner_control = 1,
                               .final_inputs_0 = 0x200c0000,
                               .final_inputs_1 = 0x1c00 };
                unsigned dots = kind == 0 ? 2 :
                                kind == 1 ? 1 :
                                            3,
                         dest = kind == 0 ? 12u << 4 :
                                kind == 1 ? 12u :
                                            12u << 8;
                s.rgb_inputs[0] = 0x04050405;
                s.rgb_outputs[0] = dest | ((dots | maps[m]) << 12);
                allocations = 0;
                GlslcResult out;
                assert(pgraph_spv_psh(&s,
                                      (SpvPshOptions){ .ubo_binding = 1,
                                                       .tex_binding = 2,
                                                       .spirv_1_6 = version },
                                      &out));
                assert(allocations == 2);
                /* The caller key dies here. Words/layouts retain their final
                 * owners. */
                memset(&s, 0xa5, sizeof(s));
                spv_diagnostic diagnostic = NULL;
                assert(spvValidateBinary(validator, out.words, out.num_words,
                                         &diagnostic) == SPV_SUCCESS);
                spvDiagnosticDestroy(diagnostic);
                uint32_t scalar = 0, ids[2] = { 0 };
                unsigned count = 0;
                for (size_t i = 5; i < out.num_words;) {
                    unsigned n = out.words[i] >> 16, op = out.words[i] & 65535;
                    assert(n && i + n <= out.num_words);
                    if (op == SpvOpTypeFloat && out.words[i + 2] == 32)
                        scalar = out.words[i + 1];
                    if (op == SpvOpDot) {
                        assert(count < 2 && out.words[i + 1] == scalar);
                        ids[count++] = out.words[i + 2];
                    }
                    i += n;
                }
                assert(count == (kind == 2 ? 2u : 1u));
                bool mapped_scalar = false, early_rgb = false;
                for (size_t i = 5; i < out.num_words;) {
                    unsigned n = out.words[i] >> 16, op = out.words[i] & 65535;
                    if (op == SpvOpCompositeConstruct)
                        for (unsigned k = 3; k < n; k++)
                            for (unsigned d = 0; d < count; d++)
                                early_rgb |= out.words[i + k] == ids[d];
                    if (op == SpvOpFAdd || op == SpvOpFSub || op == SpvOpFMul ||
                        op == SpvOpFDiv)
                        for (unsigned k = 3; k < n; k++)
                            for (unsigned d = 0; d < count; d++)
                                if (out.words[i + k] == ids[d]) {
                                    assert(out.words[i + 1] == scalar);
                                    mapped_scalar = true;
                                }
                    i += n;
                }
                if (!mapped_scalar || early_rgb)
                    fprintf(stderr, "mapping-order failure: validated module "
                                    "maps dot after RGB construction\n");
                assert(mapped_scalar && !early_rgb);
                glslc_result_free_words(&out);
                free(out.uniforms.members);
                free(out.push_constants.members);
                cases++;
            }
    spvContextDestroy(validator);
    printf("PASS: %u scalar-dot mapping/type/final-owner cases, two "
           "allocations each, Vulkan SPIR-V valid\n",
           cases);
    return 0;
}

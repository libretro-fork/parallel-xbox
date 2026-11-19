/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"

static const char *sources[] = {
    "#version 450\nvoid main(){gl_Position=vec4(0.0,1.0,-0.0,1.0);}\n",
    "#version 450\nlayout(points)in;layout(points,max_vertices=1)out;"
    "void main(){gl_Position=gl_in[0].gl_Position;EmitVertex();EndPrimitive();}\n",
    "#version 450\nlayout(location=0)out vec4 color;void main(){color=vec4(-0.0,1.0,0.0,1.0);}\n",
    "#version 450\nlayout(local_size_x=1)in;layout(set=0,binding=0,std430)buffer B{float x;}b;"
    "void main(){b.x=-0.0;}\n",
};

static void check_modes(const GlslcResult *r, bool enabled)
{
    unsigned capabilities = 0, modes = 0, entry = 0;
    assert(r->num_words >= 5 && r->words[0] == SpvMagicNumber);
    for (size_t at = 5; at < r->num_words;) {
        const uint32_t *w = r->words + at;
        unsigned op = w[0] & 65535u, count = w[0] >> 16;
        assert(count && count <= r->num_words - at);
        if (op == SpvOpEntryPoint) {
            assert(!entry && count >= 4);
            entry = w[2];
        }
        if (op == SpvOpCapability && w[1] == SpvCapabilitySignedZeroInfNanPreserve) {
            assert(count == 2);
            capabilities++;
        }
        if (op == SpvOpExecutionMode && w[2] == SpvExecutionModeSignedZeroInfNanPreserve) {
            assert(entry && count == 4 && w[1] == entry && w[3] == 32);
            modes++;
        }
        at += count;
    }
    assert(capabilities == (unsigned)enabled && modes == (unsigned)enabled);
}

/* The option may add exactly its two instructions. Every other byte, including
 * arithmetic, IDs, declaration order and interfaces, remains the same. */
static void compare_without_modes(const GlslcResult *plain, const GlslcResult *preserved)
{
    assert(preserved->num_words == plain->num_words + 6);
    assert(!memcmp(plain->words, preserved->words, 5 * sizeof(uint32_t)));
    size_t pa = 5, pb = 5;
    while (pb < preserved->num_words) {
        const uint32_t *w = preserved->words + pb;
        unsigned op = w[0] & 65535u, count = w[0] >> 16;
        bool skip = (op == SpvOpCapability && w[1] == SpvCapabilitySignedZeroInfNanPreserve) ||
                    (op == SpvOpExecutionMode && w[2] == SpvExecutionModeSignedZeroInfNanPreserve);
        if (!skip) {
            assert(count <= plain->num_words - pa);
            assert(!memcmp(plain->words + pa, w, count * sizeof(*w)));
            pa += count;
        }
        pb += count;
    }
    assert(pa == plain->num_words);
}

static void release(GlslcResult *r)
{
    glslc_result_free_words(r);
    free(r->uniforms.members);
    free(r->push_constants.members);
}

int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    bool baseline = argc == 3;
    unsigned checks = 0;
    for (unsigned target = 0; target < 2; target++) {
        for (unsigned prefix = 0; prefix < 2; prefix++) {
            for (unsigned stage = 0; stage < 4; stage++) {
                GlslcResult result[2] = {0};
                for (unsigned enabled = 0; enabled < (baseline ? 1u : 2u); enabled++) {
                    GlslcOptions opts = { .spirv_1_6 = target, .emit_layout = true,
                        .preserve_float32 = enabled, .module_prefix_words = prefix ? 256 : 0 };
                    assert(glslc_compile(stage, sources[stage], &opts, &result[enabled]));
                    assert(result[enabled].words[1] == (target ? 0x00010600u : 0x00010500u));
                    check_modes(&result[enabled], enabled);
                    char path[4096];
                    int n = snprintf(path, sizeof(path), "%s/%u-%u-%u-%u.spv", argv[1], target, prefix, stage, enabled);
                    assert(n > 0 && (size_t)n < sizeof(path));
                    FILE *file = fopen(path, "wb");
                    assert(file && fwrite(result[enabled].words, sizeof(uint32_t), result[enabled].num_words, file) == result[enabled].num_words);
                    assert(!fclose(file));
                    checks++;
                }
                if (!baseline) compare_without_modes(&result[0], &result[1]);
                release(&result[0]);
                if (!baseline) release(&result[1]);
            }
        }
    }
    printf("FP32 header checks: %u modules, four stages, both SPIR-V targets and final-allocation prefix modes\n", checks);
    return 0;
}

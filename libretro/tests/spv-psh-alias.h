/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEST_SPV_PSH_ALIAS_H
#define TEST_SPV_PSH_ALIAS_H
#include <assert.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
static uint32_t alias_inputs(unsigned seed, bool alpha)
{
    const unsigned regs[] = { 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14 };
    uint32_t v = 0;
    for (unsigned j = 0; j < 4; j++) {
        unsigned r = regs[(seed + j * 5) % 13],
                 mapping = alpha ? ((seed + j) & 1 ? 0x20 : 0) :
                                   ((seed + j * 3) & 7) << 5,
                 channel = alpha ? 16 : ((seed >> j) & 1 ? 16 : 0);
        v = (v << 8) | r | mapping | channel;
    }
    return v;
}
static void alias_state(PshState *s, unsigned f)
{
    assert(f < 2048);
    memset(s, 0, sizeof(*s));
    const unsigned dests[] = { 4, 5, 12, 13 };
    s->depth_format = DEPTH_FORMAT_D24;
    s->smooth_shading = !!(f & 1);
    s->combiner_control = (1 + f / 256) | ((PS_COMBINERCOUNT_MUX_MSB |
                                            PS_COMBINERCOUNT_UNIQUE_C0 |
                                            PS_COMBINERCOUNT_UNIQUE_C1)
                                           << 8);
    for (unsigned j = 0; j < (s->combiner_control & 255); j++) {
        unsigned n = f + j * 37;
        s->rgb_inputs[j] = alias_inputs(n, false);
        s->alpha_inputs[j] = alias_inputs(n + 5, true);
        unsigned out =
            dests[n % 4] << 4 | dests[(n / 4) % 4] | dests[(n / 16) % 4] << 8;
        s->rgb_outputs[j] = out | ((n & 255) << 12);
        s->alpha_outputs[j] = out | ((n & 4) << 12);
    }
    unsigned reg = dests[(f / 64) % 4];
    s->final_inputs_0 = 0x20000000u | (reg << 16);
    /* Explicit E reference declares R0 even when a mux is its only other read.
     * The unused EF product does not change the selected final register. */
    s->final_inputs_1 = (PS_REGISTER_R0 << 24) | ((reg | 16u) << 8);
}
#endif

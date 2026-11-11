/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEST_SPV_PSH_COMPOSITION_H
#define TEST_SPV_PSH_COMPOSITION_H
#include <assert.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
/* Same state fixture for compiler parity and numerical GPU comparisons.
 * All four texture results feed one static combiner; no source is dead. */
static void composition_state(PshState *s, unsigned choice, unsigned profile)
{
    assert(choice < 1296 && profile < 24);
    const unsigned modes[] = {
        PS_TEXTUREMODES_NONE,      PS_TEXTUREMODES_PASSTHRU,
        PS_TEXTUREMODES_CLIPPLANE, PS_TEXTUREMODES_PROJECT2D,
        PS_TEXTUREMODES_PROJECT3D, PS_TEXTUREMODES_CUBEMAP
    };
    const unsigned kernels[] = { CONVOLUTION_FILTER_DISABLED,
                                 CONVOLUTION_FILTER_QUINCUNX,
                                 CONVOLUTION_FILTER_GAUSSIAN };
    memset(s, 0, sizeof(*s));
    s->depth_format = DEPTH_FORMAT_D24;
    s->combiner_control = 1;
    s->rgb_inputs[0] = 0x08090a0b;
    s->alpha_inputs[0] = 0x18191a1b;
    s->rgb_outputs[0] = s->alpha_outputs[0] = PS_REGISTER_R0 << 8;
    s->final_inputs_0 = 0x200c0000;
    s->final_inputs_1 = 0x1c00;
    for (unsigned unit = 0; unit < 4; unit++) {
        unsigned mode = modes[choice % 6];
        choice /= 6;
        s->shader_stage_program |= mode << (unit * 5);
        s->dim_tex[unit] = mode == PS_TEXTUREMODES_PROJECT3D ? 3 : 2;
        s->tex_cubemap[unit] = mode == PS_TEXTUREMODES_CUBEMAP;
        s->conv_tex[unit] = kernels[profile % 3];
        s->colorkey_mode[unit] = (profile / 3) % 4;
    }
    s->point_sprite = profile / 12;
}
#endif

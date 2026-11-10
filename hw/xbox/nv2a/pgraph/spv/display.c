/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "display.h"
#include "display-words.inc"

static const GlslcUniform display_push_constants[] = {
    {.name="line_offset", .dim_v=1, .dim_a=1, .offset=0},
    {.name="display_size", .dim_v=2, .dim_a=1, .offset=8},
    {.name="pvideo_enable", .dim_v=1, .dim_a=1, .offset=16},
    {.name="pvideo_in_pos", .dim_v=2, .dim_a=1, .offset=24},
    {.name="pvideo_pos", .dim_v=4, .dim_a=1, .offset=32},
    {.name="pvideo_scale", .dim_v=4, .dim_a=1, .offset=48},
    {.name="pvideo_color_key_enable", .dim_v=1, .dim_a=1, .offset=64},
    {.name="pvideo_color_key", .dim_v=3, .dim_a=1, .offset=80},
    {.name="source_transform", .dim_v=4, .dim_a=1, .offset=96},
    {.name="source_bytes", .dim_v=4, .dim_a=1, .offset=112},
};

SpvDisplayShader pgraph_spv_display(bool byte_scanout, bool spirv_1_6)
{
    static const SpvStaticModule modules[2][2] = {
        {{display_0_0, sizeof(display_0_0)/4}, {display_0_1, sizeof(display_0_1)/4}},
        {{display_1_0, sizeof(display_1_0)/4}, {display_1_1, sizeof(display_1_1)/4}},
    };
    return (SpvDisplayShader){
        .module=modules[spirv_1_6][!byte_scanout],
        .push_constants=display_push_constants,
        .num_push_constants=sizeof(display_push_constants)/sizeof(display_push_constants[0]),
        .push_constant_size=128,
    };
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Frozen 468b793750 projection; only dynamic stencil identity may differ. */
static inline void reference_reg_values(PGRAPHState *pg, uint32_t *out)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    /* Only the bits create_pipeline() consumes enter the key: any other bit
     * (e.g. animated ZOFFSET* floats) would make duplicate pipelines. */
    static const uint32_t reg_masks[] = {
        /* BLEND: EQN | EN | SFACTOR | DFACTOR */
        PGRAPH_BLEND_STATE_MASK,
        /* BLENDCOLOR: constants, consumed whole */
        0xFFFFFFFF,
        /* CONTROL_0: ZENABLE | ZFUNC | Z/STENCIL/RGBA WRITE_ENABLE */
        0x3F0F4000,
        /* CONTROL_1: STENCIL_TEST | FUNC | REF | MASK_READ | MASK_WRITE */
        0xFFFFFFF1,
        /* CONTROL_2: STENCIL_OP_FAIL | ZFAIL | ZPASS */
        0x00000FFF,
        /* CONTROL_3: not consumed */
        0x00000000,
        /* SETUPRASTER: FRONTFACE | CULLENABLE | CULLCTRL */
        0x10E00000,
        /* ZOFFSETBIAS/FACTOR: not consumed (depthBiasEnable hardcoded off) */
        0x00000000,
        0x00000000,
    };
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(pipeline_registers) != ARRAY_SIZE(((PipelineKey *)0)->regs));
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(reg_masks) != ARRAY_SIZE(pipeline_registers));
    for (int i = 0; i < ARRAY_SIZE(pipeline_registers); i++) {
        out[i] = pgraph_reg_r(pg, pipeline_registers[i]) & reg_masks[i];
    }
    /* State set dynamically does not identify a pipeline. */
    out[1] = 0; /* BLENDCOLOR: VK_DYNAMIC_STATE_BLEND_CONSTANTS */
    if (r->dyn_blend) {
        out[0] = 0;
        out[2] &= ~(NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE |
                    NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE |
                    NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE |
                    NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE);
    } else {
        out[0] = pgraph_surface_blend(out[0],
                                      pg->surface_shape.color_format);
    }
    if (r->dyn_cull_front) {
        out[6] = 0;
        out[2] &= ~(NV_PGRAPH_CONTROL_0_ZENABLE |
                    NV_PGRAPH_CONTROL_0_ZFUNC |
                    NV_PGRAPH_CONTROL_0_ZWRITEENABLE);
        out[3] = 0;
        out[4] = 0;
    }
}

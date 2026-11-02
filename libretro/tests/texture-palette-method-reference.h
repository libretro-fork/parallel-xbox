/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Frozen e052e45 method body for state/publication comparison. */
{
    int slot = (method - NV097_SET_TEXTURE_PALETTE) / 64;

    bool dma_select =
        GET_MASK(parameter, NV097_SET_TEXTURE_PALETTE_CONTEXT_DMA) == 1;
    unsigned int length =
        GET_MASK(parameter, NV097_SET_TEXTURE_PALETTE_LENGTH);
    unsigned int offset =
        GET_MASK(parameter, NV097_SET_TEXTURE_PALETTE_OFFSET);

    unsigned int reg = NV_PGRAPH_TEXPALETTE0 + slot * 4;
    uint32_t prev = pgraph_reg_r(pg, reg);
    PG_SET_MASK(reg, NV_PGRAPH_TEXPALETTE0_CONTEXT_DMA, dma_select);
    PG_SET_MASK(reg, NV_PGRAPH_TEXPALETTE0_LENGTH, length);
    PG_SET_MASK(reg, NV_PGRAPH_TEXPALETTE0_OFFSET, offset);

    if (pgraph_reg_r(pg, reg) != prev) {
        pg->texture_dirty[slot] = true;
    }
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Frozen 68efb96 method body for state and publication-count comparison. */
{
    int slot = (method - NV097_SET_TEXTURE_FORMAT) / 64;

    bool dma_select =
        GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA) == 2;
    bool cubemap =
        GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_CUBEMAP_ENABLE);
    unsigned int border_source =
        GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE);
    unsigned int dimensionality =
        GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY);
    unsigned int color_format =
        GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_COLOR);
    unsigned int levels =
        GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS);
    unsigned int log_width =
        GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U);
    unsigned int log_height =
        GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V);
    unsigned int log_depth =
        GET_MASK(parameter, NV097_SET_TEXTURE_FORMAT_BASE_SIZE_P);

    unsigned int reg = NV_PGRAPH_TEXFMT0 + slot * 4;
    uint32_t prev = pgraph_reg_r(pg, reg);
    PG_SET_MASK(reg, NV_PGRAPH_TEXFMT0_CONTEXT_DMA, dma_select);
    PG_SET_MASK(reg, NV_PGRAPH_TEXFMT0_CUBEMAPENABLE, cubemap);
    PG_SET_MASK(reg, NV_PGRAPH_TEXFMT0_BORDER_SOURCE, border_source);
    PG_SET_MASK(reg, NV_PGRAPH_TEXFMT0_DIMENSIONALITY, dimensionality);
    PG_SET_MASK(reg, NV_PGRAPH_TEXFMT0_COLOR, color_format);
    PG_SET_MASK(reg, NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS, levels);
    PG_SET_MASK(reg, NV_PGRAPH_TEXFMT0_BASE_SIZE_U, log_width);
    PG_SET_MASK(reg, NV_PGRAPH_TEXFMT0_BASE_SIZE_V, log_height);
    PG_SET_MASK(reg, NV_PGRAPH_TEXFMT0_BASE_SIZE_P, log_depth);

    if (pgraph_reg_r(pg, reg) != prev) {
        pg->texture_dirty[slot] = true;
    }
}

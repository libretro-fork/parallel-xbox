/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Frozen d99a0a5 method body for state/publication comparison. */
{
    /* PGRAPH channels are ARGB, parameter channels are ABGR */
    uint8_t red = GET_MASK(parameter, NV097_SET_FOG_COLOR_RED);
    uint8_t green = GET_MASK(parameter, NV097_SET_FOG_COLOR_GREEN);
    uint8_t blue = GET_MASK(parameter, NV097_SET_FOG_COLOR_BLUE);
    uint8_t alpha = GET_MASK(parameter, NV097_SET_FOG_COLOR_ALPHA);
    PG_SET_MASK(NV_PGRAPH_FOGCOLOR, NV_PGRAPH_FOGCOLOR_RED, red);
    PG_SET_MASK(NV_PGRAPH_FOGCOLOR, NV_PGRAPH_FOGCOLOR_GREEN, green);
    PG_SET_MASK(NV_PGRAPH_FOGCOLOR, NV_PGRAPH_FOGCOLOR_BLUE, blue);
    PG_SET_MASK(NV_PGRAPH_FOGCOLOR, NV_PGRAPH_FOGCOLOR_ALPHA, alpha);
}

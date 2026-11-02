/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Frozen 481c41b method, including pre-write surface-cache accounting. */
{
    pg->surface_color.write_enabled_cache |= pgraph_color_write_enabled(pg);

    bool alpha = parameter & NV097_SET_COLOR_MASK_ALPHA_WRITE_ENABLE;
    bool red = parameter & NV097_SET_COLOR_MASK_RED_WRITE_ENABLE;
    bool green = parameter & NV097_SET_COLOR_MASK_GREEN_WRITE_ENABLE;
    bool blue = parameter & NV097_SET_COLOR_MASK_BLUE_WRITE_ENABLE;
    PG_SET_MASK(NV_PGRAPH_CONTROL_0,
             NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE, alpha);
    PG_SET_MASK(NV_PGRAPH_CONTROL_0,
             NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE, red);
    PG_SET_MASK(NV_PGRAPH_CONTROL_0,
             NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE, green);
    PG_SET_MASK(NV_PGRAPH_CONTROL_0,
             NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE, blue);
}

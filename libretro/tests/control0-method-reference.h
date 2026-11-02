/* Frozen from 1c793e4; LGPL-2.1-or-later. */
{
    pgraph_renderer(&d->pgraph)->ops.surface_update(d, false, true, true);

    bool stencil_write_enable =
        parameter & NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE;
    PG_SET_MASK(NV_PGRAPH_CONTROL_0,
             NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE,
             stencil_write_enable);

    uint32_t z_format = GET_MASK(parameter, NV097_SET_CONTROL0_Z_FORMAT);
    PG_SET_MASK(NV_PGRAPH_SETUPRASTER,
             NV_PGRAPH_SETUPRASTER_Z_FORMAT, z_format);

    bool z_perspective =
        parameter & NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE;
    PG_SET_MASK(NV_PGRAPH_CONTROL_0,
             NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE,
             z_perspective);
}

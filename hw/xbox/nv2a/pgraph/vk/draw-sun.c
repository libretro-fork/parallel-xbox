/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "qemu/osdep.h"
#include "renderer.h"
#include "sun-native.h"

#define SUN_STREAM 1
#define SUN_FLUSH_DRAW pgraph_vk_sun_flush_draw
#define begin_pre_draw pgraph_vk_sun_begin_pre_draw
#define begin_draw pgraph_vk_sun_begin_draw
#define end_draw pgraph_vk_sun_end_draw
#define vk_cost_probe_active pgraph_vk_sun_cost_active
#include "draw-stream.inc"

void pgraph_vk_sun_draw_end(NV2AState *d)
{
    PGRAPHState *pg=&d->pgraph;
    PGRAPHVkState *r=pg->vk_renderer_state;
    uint32_t control=pgraph_reg_r(pg,NV_PGRAPH_CONTROL_0);
    bool color=control & (NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE|NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE|
        NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE|NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE);
    bool depth=control & NV_PGRAPH_CONTROL_0_ZENABLE;
    bool stencil=pgraph_reg_r(pg,NV_PGRAPH_CONTROL_1)&NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;
    if(!(color||depth||stencil))return;
    pgraph_vk_sun_flush_draw(d);
    if(pgraph_vk_sun_cost_active(pg))xemu_cost_draw_end_tally(pg);
    pg->draw_time++;
    if(r->color_binding && pgraph_color_write_enabled(pg))r->color_binding->draw_time=pg->draw_time;
    if(r->zeta_binding && pgraph_zeta_write_enabled(pg))r->zeta_binding->draw_time=pg->draw_time;
    pgraph_vk_set_surface_dirty(pg,color,depth||stencil);
    pgraph_vk_sun_eager_submit(pg);
}

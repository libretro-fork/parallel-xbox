/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_VK_SUN_NATIVE_H
#define NV2A_VK_SUN_NATIVE_H
#include "../sun-shadow-fifo.h"
#include "../sun-shadow-hooks.h"

void pgraph_vk_sun_flush_draw(NV2AState *d);
void pgraph_vk_sun_draw_end(NV2AState *d);
void pgraph_vk_sun_begin_pre_draw(PGRAPHState *pg);
void pgraph_vk_sun_begin_draw(PGRAPHState *pg);
void pgraph_vk_sun_end_draw(PGRAPHState *pg);
bool pgraph_vk_sun_cost_active(PGRAPHState *pg);
void pgraph_vk_sun_eager_submit(PGRAPHState *pg);
void pgraph_vk_sun_receiver_bind(PGRAPHState *pg);
void pgraph_vk_sun_caster_record(PGRAPHState *pg);
void pgraph_vk_sun_borrow_streams(PGRAPHState *pg, const VkBuffer *buffers,
                                  const VkDeviceSize *offsets, unsigned count);
void pgraph_vk_sun_borrow_index(PGRAPHState *pg, VkBuffer buffer, VkDeviceSize offset);
bool pgraph_vk_sun_receiver_pipeline(PGRAPHState *pg, VkShaderModule fragment,
    VkPipelineLayout *layout, VkPipeline *pipeline, VkDescriptorSetLayout sun_layout);
#endif

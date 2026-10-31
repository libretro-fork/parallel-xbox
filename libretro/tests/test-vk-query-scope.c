/* SPDX-License-Identifier: GPL-2.0-or-later */
#define VK_NO_PROTOTYPES
#include "vk-query-fixture.h"
typedef struct {bool safe,available,active,pending_reset;unsigned scope;uint64_t samples;} Slot;
static Slot slots[1024];static unsigned pass_id;static bool game_active,cost_active,stats_active;
static PGRAPHVkState *state;
static void vkResetQueryPool(VkDevice device,VkQueryPool pool,uint32_t first,uint32_t count) {
    assert(state->host_query_reset && pool==state->query_pool && count==1 && slots[first].safe);
    slots[first].available=false;slots[first].samples=0;
}
static void vkCmdResetQueryPool(VkCommandBuffer cmd,VkQueryPool pool,uint32_t first,uint32_t count) {
    assert(!state->host_query_reset && !state->in_render_pass && count==1);slots[first].pending_reset=true;
}
static void vkCmdBeginQuery(VkCommandBuffer cmd,VkQueryPool pool,uint32_t index,VkQueryControlFlags flags) {
    if(pool==state->query_pool) {
        assert(!game_active && !cost_active && !slots[index].active);
        assert(state->host_query_reset?!slots[index].available:slots[index].pending_reset);
        game_active=true;slots[index].active=true;slots[index].safe=false;slots[index].samples=0;
        slots[index].scope=state->in_render_pass?pass_id:0;
    } else if(pool==state->cost_occl_pool) {assert(!cost_active && !game_active);cost_active=true;}
    else {assert(pool==state->cost_stats_pool && !stats_active);stats_active=true;}
}
static void vkCmdEndQuery(VkCommandBuffer cmd,VkQueryPool pool,uint32_t index) {
    if(pool==state->query_pool) {
        assert(game_active && slots[index].active);
        assert(slots[index].scope?(state->in_render_pass && slots[index].scope==pass_id):!state->in_render_pass);
        game_active=false;slots[index].active=false;
    } else if(pool==state->cost_occl_pool) {assert(cost_active);cost_active=false;}
    else {assert(stats_active);stats_active=false;}
}
static void vkCmdBeginRenderPass(VkCommandBuffer cmd,const VkRenderPassBeginInfo *info,VkSubpassContents contents) {
    assert(!state->in_render_pass);pass_id++;
}
static void vkCmdEndRenderPass(VkCommandBuffer cmd) {
    assert(state->in_render_pass && !cost_active && !stats_active);
    if(game_active) assert(!state->query_in_render_pass);
}
static void vkCmdBindPipeline(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipeline pipeline) {assert(state->in_render_pass);}
static void vkCmdSetViewport(VkCommandBuffer cmd,uint32_t first,uint32_t n,const VkViewport *v) {assert(n==1 && v->width==8 && v->height==8);}
static void vkCmdSetScissor(VkCommandBuffer cmd,uint32_t first,uint32_t n,const VkRect2D *s) {assert(n==1);}
static void vkCmdSetLineWidth(VkCommandBuffer cmd,float width) {}
/* PRODUCTION */
#include "vk-query-workload.h"
static void draw_triangle(PGRAPHState *pg) {
    assert(state->in_draw && state->in_render_pass);
    if(game_active) slots[state->num_queries_in_flight-1].samples+=64;
}
int main(void) {
    unsigned cases=0;
    for(unsigned host=0;host<2;host++) for(unsigned cost=0;cost<2;cost++) for(unsigned mixed=0;mixed<2;mixed++) {
        memset(slots,0,sizeof(slots));for(unsigned j=0;j<1024;j++) {slots[j].safe=true;slots[j].available=true;}
        /* Repeat with previously available values still in the pool. Host
         * reset must make recycled slots unavailable before submission. */
        for(unsigned cycle=0;cycle<64;cycle++) {
            PGRAPHVkState r={0};PipelineBinding pipeline={0};state=&r;
            r.query_pool=(VkQueryPool)(uintptr_t)1;r.cost_stats_pool=(VkQueryPool)(uintptr_t)2;r.cost_occl_pool=(VkQueryPool)(uintptr_t)3;
            r.host_query_reset=host;r.in_command_buffer=true;r.max_queries_in_flight=1024;
            r.pipeline_binding=&pipeline;r.pipeline_binding_changed=true;r.framebuffer=(VkFramebuffer)(uintptr_t)1;
            r.cost_pending_slot=-1;r.cost_pools_reset=cost;
            NV2AState d={.pgraph={.vk_renderer_state=&r,.surface_scale_factor=1,.surface_binding_dim={8,8},.surface_shape={0,0,8,8}}};
            cost_model=cost;cost_slots=interruptions=0;xemu_cost_q_open=false;
            memset(counters,0,sizeof(counters));game_active=cost_active=stats_active=false;pass_id=0;
            workload(&d,mixed);uint64_t results[1024];
            for(int j=0;j<r.num_queries_in_flight;j++) {
                assert(!slots[j].active && !slots[j].safe);
                slots[j].safe=slots[j].available=true;slots[j].pending_reset=false;results[j]=slots[j].samples;
            }
            check_results(results);cases++;
        }
    }
    printf("query scope: %u host/device-reset, cost-model, pass-break, clear/disable and recycled-result workloads passed\n",cases);
    puts("237 consecutive query windows: 237 render passes -> 1 with host query reset");
}

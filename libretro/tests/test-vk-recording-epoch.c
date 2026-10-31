/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define PGRAPH_VK_SLOTS 2
#define XEMU_COST_SEGS 8
#define NV2A_PROF_QUEUE_SUBMIT 0
#define g_renew(t,p,n) ((t *)realloc(p,sizeof(t)*(n)))
typedef struct { int unused; } Lru;
typedef struct { int unused; } LruNode;
typedef struct { LruNode node; unsigned draw_time; VkPipeline pipeline; VkPipelineLayout layout; } PipelineBinding;
struct XemuVkTrashEntry { VkPipeline pipeline; VkPipelineLayout layout; unsigned retire_after,display_after; };
enum { BUFFER_INDEX_STAGING, BUFFER_INDEX, BUFFER_VERTEX_INLINE_STAGING,
       BUFFER_VERTEX_INLINE, BUFFER_UNIFORM_STAGING, BUFFER_UNIFORM };
/* ENUM */
typedef struct {
    Lru pipeline_cache;
    PipelineBinding *pipeline_binding;
    struct XemuVkTrashEntry *trash;
    unsigned trash_n,trash_cap;
    struct { unsigned submitted; } display;
    VkDevice device;
    bool in_draw,in_command_buffer,in_render_pass,query_in_flight;
    int debug_depth,slot;
    struct { size_t buffer_offset; } storage_buffers[6];
    struct { unsigned w,w_end[2]; } frame_gpu;
    struct { bool valid; } dyn_last,vi_last;
    VkCommandBuffer command_buffer,slot_main[2],slot_sync[2];
    VkSemaphore slot_sem[2];
    VkFence slot_fence[2];
    bool slot_fence_pending[2],vertex_arena_used,budget_check_pending,cost_pools_reset;
    unsigned submit_count,slot_submit[2],allocator_last_submit_index;
    unsigned queries_submitted,num_queries_in_flight,command_buffer_start_time;
    unsigned descriptor_set_base,descriptor_set_index;
    VkDescriptorSet descriptor_sets[32];
    VkQueryPool cost_stats_pool,cost_occl_pool;
    void *allocator;
    double submit_us,cb_begin_us;
} PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; int draw_time; } PGRAPHState;
typedef struct { PGRAPHState pgraph; } NV2AState;
static const struct { int staging; } pgraph_vk_staged_buffers[]={{0},{2},{4}};
static unsigned submits,waits,reclaims,reports,teardowns;
static unsigned xemu_cost_segn;
static bool cost_model;
static int pgraph_vk_cost_ops;
static bool xemu_cost_model_active(void) { return cost_model; }
static void nv2a_profile_inc_counter(int n) { (void)n; }
static void end_render_pass(PGRAPHVkState *r) { r->in_render_pass=false; }
static void end_query(PGRAPHVkState *r) { r->query_in_flight=false; }
static void xemu_cost_frame_close(NV2AState *d,int *ops) { (void)d;(void)ops; }
static void pgraph_vk_frame_gpu_cb_end(PGRAPHVkState *r,VkCommandBuffer cmd) { (void)r;(void)cmd; }
static void pgraph_vk_frame_gpu_cb_begin(PGRAPHVkState *r,VkCommandBuffer cmd) { (void)r;(void)cmd; }
static VkResult vkEndCommandBuffer(VkCommandBuffer cmd) { (void)cmd;return VK_SUCCESS; }
static VkResult vkBeginCommandBuffer(VkCommandBuffer cmd,const VkCommandBufferBeginInfo *info) {
    (void)cmd;assert(info->flags==VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);return VK_SUCCESS;
}
static VkResult vkResetFences(VkDevice dev,uint32_t n,const VkFence *fences) {
    (void)dev;(void)fences;assert(n==1);return VK_SUCCESS;
}
static void sync_staging_buffer(PGRAPHState *pg,VkCommandBuffer cmd,int src,int dst) {
    (void)cmd;assert(dst==src+1);pg->vk_renderer_state->storage_buffers[src].buffer_offset=0;
}
static void flush_memory_buffer(PGRAPHState *pg,VkCommandBuffer cmd) { (void)pg;(void)cmd; }
static int64_t g_get_monotonic_time(void) { static int64_t n;return ++n; }
static void ema16(double *v,int64_t us) { *v=us; }
static void pgraph_vk_queue_submit(PGRAPHVkState *r,uint32_t n,const VkSubmitInfo *info,VkFence fence) {
    assert(r->in_command_buffer && n==2 && fence==r->slot_fence[r->slot]);
    assert(info[0].commandBufferCount==1 && *info[0].pCommandBuffers==r->slot_sync[r->slot]);
    assert(info[1].commandBufferCount==1 && *info[1].pCommandBuffers==r->command_buffer);
    assert(info[0].signalSemaphoreCount==1 && info[1].waitSemaphoreCount==1);
    assert(*info[0].pSignalSemaphores==*info[1].pWaitSemaphores);submits++;
}
static void vmaSetCurrentFrameIndex(void *allocator,unsigned frame) { (void)allocator;(void)frame; }
static void rotate_slot(PGRAPHVkState *r,bool staged) {
    (void)staged;assert(!r->in_command_buffer && r->slot_fence_pending[r->slot]);
    r->slot=(r->slot+1)%2;r->command_buffer=r->slot_main[r->slot];
}
static void pgraph_vk_wait_all_slots(PGRAPHVkState *r) {
    waits++;r->slot_fence_pending[0]=r->slot_fence_pending[1]=false;
}
static void teardown_after_settle(PGRAPHState *pg) {
    assert(!pg->vk_renderer_state->in_command_buffer);teardowns++;
}
static void pgraph_vk_settle_all_slots(PGRAPHState *pg) {
    pgraph_vk_wait_all_slots(pg->vk_renderer_state);teardown_after_settle(pg);
}
static void pgraph_vk_process_pending_reports_internal(NV2AState *d) { (void)d;reports++; }
static void pgraph_vk_reclaim_command_buffer(PGRAPHState *pg,bool all) {
    PGRAPHVkState *r=pg->vk_renderer_state;assert(all);reclaims++;
    r->slot_fence_pending[r->slot]=false;
}
static void vkCmdResetQueryPool(VkCommandBuffer cmd,VkQueryPool pool,unsigned first,unsigned count) {
    (void)cmd;(void)pool;assert(first==xemu_cost_segn && count==XEMU_COST_SEGS-first);
}
static const int finish_reason_to_counter_enum[VK_FINISH_REASON_EAGER+1]={0};
/* PRODUCTION */
int main(void) {
    unsigned cases=0;
    for(unsigned model=0;model<2;model++)
    for(unsigned why=0;why<=VK_FINISH_REASON_EAGER;why++)
    for(unsigned staged=0;staged<2;staged++) {
        PGRAPHVkState r={0};NV2AState d={.pgraph={&r,1}};PGRAPHState *pg=&d.pgraph;
        cost_model=model;
        for(unsigned s=0;s<2;s++) {
            r.slot_main[s]=(VkCommandBuffer)(uintptr_t)(10+s);
            r.slot_sync[s]=(VkCommandBuffer)(uintptr_t)(20+s);
            r.slot_fence[s]=(VkFence)(uintptr_t)(30+s);
            r.slot_sem[s]=(VkSemaphore)(uintptr_t)(40+s);
        }
        for(unsigned frame=0;frame<1000;frame++) {
            pgraph_vk_begin_command_buffer(pg);
            assert(r.command_buffer_start_time==(unsigned)pg->draw_time);
            /* The last draw writes the surface and uses a cached pipeline. */
            unsigned stamp=++pg->draw_time;
            r.in_render_pass=r.query_in_flight=true;r.vertex_arena_used=true;
            r.num_queries_in_flight=3;
            r.storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset=staged?64:0;
            unsigned old_submits=submits,old_waits=waits,old_slot=r.slot;
            unsigned serial=r.submit_count;
            pgraph_vk_finish(pg,why);
            assert(!r.in_command_buffer && !r.in_render_pass && !r.query_in_flight);
            assert(submits==old_submits+1 && r.submit_count==serial+1);
            assert(r.queries_submitted==3 && !r.vertex_arena_used);
            assert(pg->draw_time>(int)stamp);
            if(finish_is_async(why)) {
                assert(waits==old_waits && r.slot_fence_pending[old_slot]);
            } else {
                assert(waits==old_waits+1 && !r.slot_fence_pending[old_slot]);
            }
            /* A finish with no new recording must not advance the epoch. */
            int epoch=pg->draw_time;
            pgraph_vk_finish(pg,why);assert(pg->draw_time==epoch);
            pgraph_vk_begin_command_buffer(pg);
            /* Old surface/pipeline stamps are not pending in this recording,
             * even when the previous slot's fence has not retired. */
            assert(stamp<r.command_buffer_start_time);
            if(finish_is_async(why)) assert(r.slot_fence_pending[old_slot]);
            /* A pipeline from the old recording may be evicted now, but its
             * handles must remain queued for deferred fence-based deletion. */
            PipelineBinding pipeline={.draw_time=stamp,
                .pipeline=(VkPipeline)(uintptr_t)51,.layout=(VkPipelineLayout)(uintptr_t)52};
            r.pipeline_binding=&pipeline;
            unsigned n=r.trash_n;
            pipeline_cache_entry_post_evict(&r.pipeline_cache,&pipeline.node);
            assert(r.trash_n==n+1 && r.pipeline_binding==NULL);
            assert(pipeline.pipeline==VK_NULL_HANDLE && pipeline.layout==VK_NULL_HANDLE);
            assert(r.trash[n].pipeline==(VkPipeline)(uintptr_t)51);
            assert(r.trash[n].layout==(VkPipelineLayout)(uintptr_t)52);
            assert(r.trash[n].retire_after==r.submit_count+1);
            if(finish_is_async(why)) assert(r.slot_fence_pending[old_slot]);
            /* No-op display/readback/upload inspection emits no submission. */
            assert(submits==old_submits+1);
            /* A new clear/upload or draw in this recording remains pending. */
            unsigned current=pg->draw_time;
            assert(current>=r.command_buffer_start_time);
            ++pg->draw_time;
            assert((unsigned)pg->draw_time>=r.command_buffer_start_time);
            pgraph_vk_finish(pg,VK_FINISH_REASON_FLUSH);
            cases++;
        }
        free(r.trash);
    }
    assert(reclaims && reports && teardowns);
    printf("recording epoch: %u async/sync, cost-model, staging and delayed-retirement cases passed\n",cases);
}

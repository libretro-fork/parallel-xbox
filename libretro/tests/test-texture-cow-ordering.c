/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vulkan/vulkan_core.h"
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define g_renew(t,p,n) ((t *)realloc((p),(n)*sizeof(t)))
#define NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8 42
#define VK_FINISH_REASON_TEXTURE_UP 0
#define VK_CHECK(c) assert((c)==VK_SUCCESS)
#define HANDLE(t,n) ((t)(uintptr_t)(n))
typedef void *XemuVkAllocation;
typedef struct {bool host;} XemuVkMemoryRequest;
typedef struct {unsigned color_format;} TextureShape;
typedef struct {VkFormat vk_format;} VkColorFormatInfo;
typedef struct {int unused;} LruNode;
typedef struct {int unused;} Lru;
typedef struct TextureBinding {
    LruNode node;
    bool image_referenced,logo,s2t_borrowed,depth_source_copy;
    unsigned cube_source_mask,sampled_frame,validation_frame;
    uint32_t submit_time;
    uint64_t seq;
    struct {TextureShape state;} key;
    VkImage image; VkImageView image_view; VkSampler sampler;
    XemuVkAllocation allocation; VkImageCreateInfo image_ci; VkImageLayout current_layout;
} TextureBinding;
struct XemuVkTrashEntry {
    VkSampler sampler; VkImage image; VkImageView image_view;
    XemuVkAllocation allocation; VkImageCreateInfo image_ci;
    bool pool_ok; uint64_t aux_after,retire_after,display_after;
};
typedef struct {
    bool in_command_buffer; uint32_t submit_count,settled_submit;
    uint64_t texture_binding_seq,aux_submitted;
    unsigned trash_n,trash_cap; struct XemuVkTrashEntry *trash;
    struct {uint64_t submitted;} display;
    TextureBinding *texture_bindings[4]; Lru texture_cache;
    void *allocator;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state; unsigned frame_time;} PGRAPHState;
static bool pool_hit;
static unsigned allocations,views,finishes;
static uintptr_t next_image=100;
static VkColorFormatInfo texture_vkf(PGRAPHVkState *r,unsigned f)
{(void)r;(void)f;return (VkColorFormatInfo){VK_FORMAT_B8G8R8A8_UNORM};}
static bool pgraph_vk_image_pool_get(PGRAPHVkState *r,const VkImageCreateInfo *ci,
    VkImage *image,XemuVkAllocation *allocation)
{(void)r;assert(ci->initialLayout==VK_IMAGE_LAYOUT_UNDEFINED);if(!pool_hit)return false;*image=HANDLE(VkImage,++next_image);*allocation=HANDLE(void *,next_image);return true;}
static VkResult xemu_vk_create_image(void *allocator,const VkImageCreateInfo *ci,
    const XemuVkMemoryRequest *request,VkImage *image,XemuVkAllocation *allocation)
{(void)allocator;assert(!request->host&&ci->initialLayout==VK_IMAGE_LAYOUT_UNDEFINED);allocations++;*image=HANDLE(VkImage,++next_image);*allocation=HANDLE(void *,next_image);return VK_SUCCESS;}
static void create_texture_view(PGRAPHVkState *r,TextureBinding *b,const VkColorFormatInfo *f)
{(void)r;assert(f->vk_format==VK_FORMAT_B8G8R8A8_UNORM);b->image_view=HANDLE(VkImageView,next_image+1000);views++;}
static void pgraph_vk_finish(PGRAPHState *pg,unsigned why)
{assert(why==VK_FINISH_REASON_TEXTURE_UP);finishes++;pg->vk_renderer_state->in_command_buffer=false;}
/* PRODUCTION */
int main(void)
{
    PGRAPHVkState initial={0};TextureBinding poisoned;
    memset(&poisoned,0xa5,sizeof(poisoned));
    texture_cache_entry_init(&initial.texture_cache,&poisoned.node,NULL);
    assert(!poisoned.image_referenced&&!poisoned.submit_time&&!poisoned.image);
    assert(poisoned.validation_frame==UINT32_MAX);
    const uint32_t serials[]={0,1,7,100};unsigned cases=0,replacements=0,avoided=0;
    for(unsigned c=0;c<4;c++)for(unsigned stamp=0;stamp<4;stamp++)
    for(unsigned settled=0;settled<3;settled++)for(unsigned open=0;open<2;open++)
    for(unsigned counts=0;counts<2;counts++)for(unsigned refs=0;refs<2;refs++)
    for(unsigned present=0;present<2;present++)for(unsigned pool=0;pool<2;pool++){
        uint32_t current=serials[c];
        uint32_t stamps[]={0,current?current-1:0,current,current+1};
        uint32_t settleds[]={0,current>1?current-2:0,current};
        PGRAPHVkState r={.in_command_buffer=open,.submit_count=current,
            .settled_submit=settleds[settled],.texture_binding_seq=19,.aux_submitted=9,.display={5}};
        PGRAPHState pg={&r,33};
        TextureBinding b={.image_referenced=refs,.submit_time=stamps[stamp],.seq=10,
            .image=present?HANDLE(VkImage,1):VK_NULL_HANDLE,.image_view=HANDLE(VkImageView,11),
            .allocation=HANDLE(void *,21),.current_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image_ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.format=VK_FORMAT_B8G8R8A8_UNORM,
                .extent={2048,512,1},.mipLevels=4,.arrayLayers=6,.initialLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkImage old_image=b.image;VkImageCreateInfo old_ci=b.image_ci;
        bool old_open=open&&b.submit_time==current;
        bool expected=present&&refs&&((b.submit_time>r.settled_submit&&!old_open)||(counts&&old_open));
        pool_hit=pool;allocations=views=finishes=0;
        bool replaced=texture_cow_if_referenced(&pg,&b,counts);
        assert(replaced==expected);
        if(replaced){
            assert(r.trash_n==1&&r.trash[0].image==old_image&&r.trash[0].image_view==HANDLE(VkImageView,11));
            assert(r.trash[0].allocation==HANDLE(void *,21)&&r.trash[0].aux_after==9);
            assert(r.trash[0].retire_after==(uint64_t)current+open&&r.trash[0].display_after==5);
            assert(!memcmp(&r.trash[0].image_ci,&old_ci,sizeof(old_ci)));
            assert(b.image!=old_image&&!b.image_referenced&&b.submit_time==stamps[stamp]);
            assert(b.seq==20&&b.current_layout==VK_IMAGE_LAYOUT_UNDEFINED);
            assert(b.image_ci.extent.width==2048&&b.image_ci.mipLevels==4&&b.image_ci.arrayLayers==6);
            assert(views==1&&allocations==!pool);
            upload_guard(&pg,&b);assert(!finishes&&r.in_command_buffer==open);
            assert(!texture_cow_if_referenced(&pg,&b,true)&&r.trash_n==1);
            assert(texture_cache_entry_pre_evict(&r.texture_cache,&b.node));
            if(old_open)avoided++;
            replacements++;
        }else{
            assert(!r.trash_n&&!allocations&&!views&&b.image==old_image&&b.image_referenced==refs);
        }
        if(present){
            r.texture_bindings[0]=&b;update_timestamps(&pg);
            assert(b.image_referenced&&b.submit_time==current&&b.sampled_frame==33);
            r.texture_bindings[0]=NULL;r.in_command_buffer=true;
            assert(!texture_cache_entry_pre_evict(&r.texture_cache,&b.node));
            finishes=0;upload_guard(&pg,&b);assert(finishes==1);
            texture_cache_release_node_resources(&r,&b);
            assert(!b.image_referenced&&!b.image&&!b.image_view&&!b.allocation);
            assert(texture_cache_entry_pre_evict(&r.texture_cache,&b.node));
        }
        free(r.trash);cases++;
    }
    assert(replacements&&avoided);
    printf("texture COW: %u states, %u replacements, %u obsolete upload submissions avoided; retirement, redraw and release checks passed\n",cases,replacements,avoided);
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define VK_NULL_HANDLE 0
#define VK_MAX_MEMORY_HEAPS 2
#define VK_CHECK(x) assert((x)==0)
#define VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE 1
/* LIMIT */
typedef unsigned VkImage,VmaAllocation,VkImageView;
typedef struct {unsigned imageType,format;struct {unsigned width,height,depth;} extent;unsigned mipLevels,arrayLayers,samples,tiling,usage,flags,sharingMode;} VkImageCreateInfo;
typedef struct {size_t size;unsigned memoryType;} VmaAllocationInfo;
typedef struct {uint64_t usage,budget;} VmaBudget;
typedef struct {struct {unsigned heapIndex;} memoryTypes[2];} VkPhysicalDeviceMemoryProperties;
typedef struct {unsigned usage;} VmaAllocationCreateInfo;
typedef struct {struct {VkImage image;VkImageView image_view;VmaAllocation allocation;VkImageCreateInfo image_ci;} pvideo;} PGRAPHVkDisplayState;
typedef struct {
 unsigned allocator,device;PGRAPHVkDisplayState display;
 struct XemuVkPooledImage {VkImage image;VkImageView view;VmaAllocation allocation;VkImageCreateInfo ci;size_t bytes;unsigned heap;uint64_t seq;} image_pool[16];
 int image_pool_n;size_t image_pool_bytes;uint64_t image_pool_seq;
} PGRAPHVkState;
static VmaAllocationInfo allocation[10000];
static bool destroyed[10000];
static unsigned creates,destroys,view_creates,view_destroys;
static bool live_view[10000];
static uint64_t heap_usage[2],budget[2]={UINT64_MAX,UINT64_MAX};
static VkPhysicalDeviceMemoryProperties props={{{0},{1}}};
static void vmaGetHeapBudgets(unsigned a,VmaBudget *out){assert(a==7);for(unsigned i=0;i<2;i++)out[i]=(VmaBudget){heap_usage[i],budget[i]};}
static void vmaGetAllocationInfo(unsigned a,VmaAllocation im,VmaAllocationInfo *out){assert(a==7&&im&&im<=creates&&!destroyed[im]);*out=allocation[im];}
static void vmaGetMemoryProperties(unsigned a,const VkPhysicalDeviceMemoryProperties **out){assert(a==7);*out=&props;}
static int vmaCreateImage(unsigned a,const VkImageCreateInfo *ci,const VmaAllocationCreateInfo *aci,VkImage *im,VmaAllocation *mem,void *unused){
 assert(a==7&&aci->usage==1&&!unused&&creates+1<10000);*im=*mem=++creates;
 allocation[*im]=(VmaAllocationInfo){(size_t)ci->extent.width*ci->extent.height*4,ci->flags&1};heap_usage[ci->flags&1]+=allocation[*im].size;return 0;
}
static void vkDestroyImageView(unsigned d,VkImageView v,void *p){assert(d==9&&v&&live_view[v]&&!p);live_view[v]=false;view_destroys++;}
static void vmaDestroyImage(unsigned a,VkImage im,VmaAllocation mem){assert(a==7&&im==mem&&!destroyed[im]&&!live_view[im]);destroyed[im]=true;heap_usage[allocation[im].memoryType]-=allocation[im].size;destroys++;}
/* PRODUCTION */
static void acquire_view(PGRAPHVkState *r,const VkImageCreateInfo *ci){
 acquire_pvideo_image(r,ci);
 if(!r->display.pvideo.image_view){unsigned im=r->display.pvideo.image;assert(!live_view[im]);live_view[im]=true;r->display.pvideo.image_view=im;view_creates++;}
}
static void put(PGRAPHVkState *r){
 typeof(r->display.pvideo) *v=&r->display.pvideo;
 if(!pgraph_vk_image_pool_put_with_view(r,v->image,v->allocation,&v->image_ci,v->image_view)){if(v->image_view)vkDestroyImageView(r->device,v->image_view,NULL);vmaDestroyImage(r->allocator,v->image,v->allocation);}
 v->image=v->allocation=v->image_view=0;
}
int main(void){
 PGRAPHVkState r={.allocator=7,.device=9};VkImageCreateInfo ci={.imageType=1,.format=37,.extent={640,480,1},.mipLevels=1,.arrayLayers=1,.samples=1,.usage=7};
 assert(!pgraph_vk_image_pool_put(&r,0,0,&ci));
#define KEY(field) do {VkImageCreateInfo other=ci;other.field++;assert(!image_ci_equal(&ci,&other));} while(0)
 KEY(imageType);KEY(format);KEY(extent.width);KEY(extent.height);KEY(extent.depth);
 KEY(mipLevels);KEY(arrayLayers);KEY(samples);KEY(tiling);KEY(usage);KEY(flags);KEY(sharingMode);
#undef KEY
 VkImage modes[2]={0};
 for(unsigned i=0;i<20000;i++){
  unsigned mode=i%2;ci.extent.width=mode?720:640;
  acquire_view(&r,&ci);assert(image_ci_equal(&r.display.pvideo.image_ci,&ci));
  if(!r.display.pvideo.image_view){unsigned im=r.display.pvideo.image;assert(!live_view[im]);live_view[im]=true;r.display.pvideo.image_view=im;view_creates++;}
  if(!modes[mode])modes[mode]=r.display.pvideo.image;
  assert(r.display.pvideo.image==modes[mode]&&!destroyed[r.display.pvideo.image]);put(&r);
 }
 assert(creates==2&&destroys==0&&r.image_pool_n==2&&view_creates==2&&!view_destroys);
 /* A texture/surface acquisition must discard the canonical overlay view. */
 VkImage im;VmaAllocation mem;
 assert(pgraph_vk_image_pool_get(&r,&ci,&im,&mem));assert(view_destroys==1&&!live_view[im]);
 assert(pgraph_vk_image_pool_put(&r,im,mem,&ci));
 acquire_pvideo_image(&r,&ci);assert(!r.display.pvideo.image_view);put(&r);
 /* Storage-image usage and dimensions are part of the exact-match key. */
 ci.usage^=8;acquire_view(&r,&ci);assert(creates==3);put(&r);
 ci.extent.height++;acquire_view(&r,&ci);assert(creates==4);put(&r);
 pgraph_vk_image_pool_flush(&r);assert(destroys==creates&&!r.image_pool_bytes);
 /* Over-budget retirements return pooled images, then reject new retention. */
 ci.extent.height=480;acquire_view(&r,&ci);put(&r);ci.extent.width=640;acquire_view(&r,&ci);
 budget[0]=0;put(&r);assert(!r.image_pool_n&&destroys==creates&&!heap_usage[0]);budget[0]=UINT64_MAX;
 /* More distinct modes than fixed slots: evict oldest without growing storage. */
 unsigned first=creates+1;
 for(unsigned i=0;i<40;i++){ci.extent.width=640+i;acquire_view(&r,&ci);put(&r);assert(r.image_pool_n<=16);}
 assert(destroyed[first]&&r.image_pool_n==16);pgraph_vk_image_pool_flush(&r);
 /* Per-image limit rejects unusually large resources. */
 ci.extent.width=16384;ci.extent.height=16384;acquire_view(&r,&ci);put(&r);assert(!r.image_pool_n);
 /* Total-byte limit evicts resources even below the per-image limit. */
 ci.extent.height=4096;
 for(unsigned i=0;i<12;i++){ci.extent.width=16384-i;acquire_view(&r,&ci);put(&r);assert(r.image_pool_bytes<=IMAGE_POOL_MAX_BYTES);}
 assert(r.image_pool_n<12);pgraph_vk_image_pool_flush(&r);
 assert(destroys==creates&&view_creates==view_destroys&&!r.image_pool_n&&!r.image_pool_bytes&&!heap_usage[0]&&!heap_usage[1]);
 puts("PVIDEO image pool: 20000 alternating modes, two VRAM allocations and two views; cross-caller view isolation, exact usage/size keys, heap pressure, per-image/total-byte limits, fixed capacity and teardown passed");return 0;
}

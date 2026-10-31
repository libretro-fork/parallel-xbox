/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#undef assert
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
/* CLIENTS */
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define likely(x) (x)
#define unlikely(x) (x)
#define TARGET_PAGE_BITS 12
#define TARGET_PAGE_ALIGN(x) (((x)+4095)&~(uint64_t)4095)
#define DIRTY_MEMORY_BLOCK_SIZE (sizeof(unsigned long)*CHAR_BIT)
#define PAGES (DIRTY_MEMORY_BLOCK_SIZE*4)
#define RAM_SIZE (PAGES*4096)
#define WITH_RCU_READ_LOCK_GUARD() for(int once=1;once;once=0)
typedef uint64_t ram_addr_t;typedef uint64_t hwaddr;
typedef struct {unsigned long *blocks[4];} DirtyMemoryBlocks;
static unsigned long bits[DIRTY_MEMORY_NUM][4];
static DirtyMemoryBlocks clients[DIRTY_MEMORY_NUM];
static struct {void *dirty_memory[DIRTY_MEMORY_NUM];} ram_list;
static void *retro_atomic_load_acquire_ptr(void **p){return *p;}
static void bitmap_set_atomic(unsigned long *w,unsigned long offset,unsigned long count){assert(offset+count<=DIRTY_MEMORY_BLOCK_SIZE);for(unsigned long i=0;i<count;i++)*w|=1ul<<(offset+i);}
static bool xen_enabled(void){return false;}
static void xen_hvm_modified_memory(ram_addr_t a,ram_addr_t b){(void)a;(void)b;assert(false);}
static unsigned dirty_lookups;
static bool physical_memory_get_dirty_flag(ram_addr_t addr,unsigned client){dirty_lookups++;unsigned long p=(unsigned long)(addr>>12);assert(p<PAGES&&client<DIRTY_MEMORY_NUM);return !!(bits[client][p/DIRTY_MEMORY_BLOCK_SIZE]&(1ul<<(p%DIRTY_MEMORY_BLOCK_SIZE)));}
static bool physical_memory_all_dirty(ram_addr_t start,ram_addr_t length,unsigned client){for(hwaddr p=start>>12;p<(TARGET_PAGE_ALIGN(start+length)>>12);p++)if(!physical_memory_get_dirty_flag(p<<12,client))return false;return true;}
static bool physical_memory_test_and_clear_dirty(ram_addr_t start,ram_addr_t length,unsigned client){bool dirty=false;for(hwaddr p=start>>12;p<(TARGET_PAGE_ALIGN(start+length)>>12);p++){dirty|=physical_memory_get_dirty_flag(p<<12,client);bits[client][p/DIRTY_MEMORY_BLOCK_SIZE]&=~(1ul<<(p%DIRTY_MEMORY_BLOCK_SIZE));}return dirty;}
typedef struct {hwaddr base,offset;int in_width,in_height,pitch,format,in_s,out_x;} PvideoState;
typedef uintptr_t VkCommandBuffer;typedef uintptr_t VkImage;typedef uint64_t VkDeviceSize;
#define VK_FORMAT_R8G8B8A8_UNORM 1
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 1
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL 2
#define VK_IMAGE_ASPECT_COLOR_BIT 1
#define BUFFER_STAGING_SRC 0
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
typedef struct {int x,y,z;} VkOffset3D;
typedef struct {unsigned width,height,depth;} VkExtent3D;
typedef struct {VkDeviceSize bufferOffset;unsigned bufferRowLength,bufferImageHeight;struct {unsigned aspectMask,mipLevel,baseArrayLayer,layerCount;} imageSubresource;VkOffset3D imageOffset;VkExtent3D imageExtent;} VkBufferImageCopy;
typedef struct {struct {VkImage image;int width,height;bool upload_valid;hwaddr upload_addr;int upload_pitch,upload_format,upload_width,upload_height;} pvideo;} PGRAPHVkDisplayState;
typedef struct {PGRAPHVkDisplayState display;struct {uintptr_t buffer;} storage_buffers[1];} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;void *vram;uint8_t *vram_ptr;} NV2AState;
static unsigned queries;
static hwaddr memory_region_size(void *mr){assert(mr);return RAM_SIZE;}
static bool memory_region_test_and_clear_dirty(void *mr,hwaddr addr,hwaddr length,unsigned client){assert(mr&&addr+length<=RAM_SIZE&&client==DIRTY_MEMORY_NV2A_PVIDEO);queries++;return physical_memory_test_and_clear_dirty(addr,length,client);}
void physical_memory_set_dirty_range(ram_addr_t,ram_addr_t,uint8_t);
static unsigned begins,allocs,flushes,copies,ends,transitions;
static uint8_t staged[65536],gpu_image[65536];
static bool in_aux,late_write;
static hwaddr late_addr;
static NV2AState *active;
static int layout;
static bool create_pvideo_image(PGRAPHState *pg,int w,int h){
 PGRAPHVkDisplayState *d=&pg->vk_renderer_state->display;
 if(d->pvideo.image && d->pvideo.width==w && d->pvideo.height==h)return false;
 d->pvideo.image++;d->pvideo.width=w;d->pvideo.height=h;layout=VK_IMAGE_LAYOUT_UNDEFINED;return true;
}
static VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg){assert(!in_aux);active=container_of(pg,NV2AState,pgraph);in_aux=true;begins++;return 3;}
static VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *pg,size_t size,unsigned align,uint8_t **data){assert(pg==&active->pgraph&&size<=sizeof(staged)&&align==16);*data=staged;allocs++;return 0;}
static void pgraph_vk_staging_flush(PGRAPHState *pg,VkDeviceSize offset,size_t size){assert(pg==&active->pgraph&&!offset&&size<=sizeof(staged));flushes++;}
static void pgraph_vk_transition_image_layout(PGRAPHState *pg,VkCommandBuffer cmd,VkImage image,int format,int old,int next){assert(pg==&active->pgraph&&cmd==3&&image==pg->vk_renderer_state->display.pvideo.image&&format==VK_FORMAT_R8G8B8A8_UNORM&&old==layout);layout=next;transitions++;}
static void vkCmdCopyBufferToImage(VkCommandBuffer cmd,uintptr_t buffer,VkImage image,int image_layout,unsigned count,const VkBufferImageCopy *region){
 assert(in_aux&&cmd==3&&buffer==1&&image==active->pgraph.vk_renderer_state->display.pvideo.image&&count==1&&image_layout==layout&&layout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
 assert(!region->bufferOffset&&!region->bufferRowLength&&!region->bufferImageHeight&&region->imageExtent.depth==1&&region->imageSubresource.layerCount==1);
 size_t size=(size_t)region->imageExtent.width*region->imageExtent.height*4;assert(size<=sizeof(gpu_image));memcpy(gpu_image,staged,size);copies++;
}
static bool gpu_mode;
static bool pgraph_vk_pvideo_convert(PGRAPHState *pg,VkCommandBuffer cmd,const uint8_t *src,unsigned w,unsigned h,unsigned pitch,bool created){
 if(!gpu_mode)return false;
 assert(pg==&active->pgraph&&cmd==3&&src>=active->vram_ptr&&w&&h&&pitch);
 assert(layout==(created?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
 layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;copies++;return true;
}
static void pgraph_vk_end_single_time_commands_async(PGRAPHState *pg,VkCommandBuffer cmd){
 assert(in_aux&&pg==&active->pgraph&&cmd==3&&layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);in_aux=false;ends++;
 if(late_write){active->vram_ptr[late_addr]++;physical_memory_set_dirty_range(late_addr,1,(1u<<DIRTY_MEMORY_NUM)-1);}
}
static void memory_region_set_client_dirty(void *mr,hwaddr addr,hwaddr length,unsigned client){assert(mr&&client==DIRTY_MEMORY_NV2A_PVIDEO);physical_memory_set_dirty_range(addr,length,1u<<client);}
/* PRODUCTION */
static uint32_t rng=0x948fe231;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void reset(void){memset(bits,0,sizeof(bits));for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++){ram_list.dirty_memory[c]=&clients[c];for(unsigned i=0;i<4;i++)clients[c].blocks[i]=&bits[c][i];}}

static unsigned char ram[RAM_SIZE],image[1024];
int main(int argc,char **argv){
 (void)argv;gpu_mode=argc>1;
 assert(DIRTY_MEMORY_NV2A_PVIDEO!=DIRTY_MEMORY_NV2A_TEX && DIRTY_MEMORY_NUM<=8);
 unsigned cases=0;unsigned all=(1u<<DIRTY_MEMORY_NUM)-1;
 for(unsigned flags=0;flags<=all;flags++)for(unsigned mask=0;mask<=all;mask++){
  reset();physical_memory_set_dirty_range(0,4096,(uint8_t)flags);
  dirty_lookups=0;
  assert(physical_memory_is_clean(0)==(flags!=all));
  const unsigned order[]={DIRTY_MEMORY_NV2A,DIRTY_MEMORY_NV2A_TEX,DIRTY_MEMORY_NV2A_PVIDEO,DIRTY_MEMORY_VGA,DIRTY_MEMORY_CODE,DIRTY_MEMORY_MIGRATION};
  unsigned need=0;do {need++;if(!(flags&(1u<<order[need-1])))break;}while(need<DIRTY_MEMORY_NUM);
  assert(dirty_lookups==need);
  assert(physical_memory_range_includes_clean(0,4096,(uint8_t)mask)==(mask&~flags));
  physical_memory_clear_dirty_range(0,4096);for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++)assert(!physical_memory_get_dirty_flag(0,c));cases++;
 }
 for(unsigned i=0;i<20000;i++){
  reset();hwaddr start=random32()%RAM_SIZE,length=1+random32()%(RAM_SIZE-start);unsigned mask=random32()&all;
  physical_memory_set_dirty_range(start,length,(uint8_t)mask);
  for(unsigned p=0;p<PAGES;p++)for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++)assert(physical_memory_get_dirty_flag((hwaddr)p*4096,c)==((p>=start/4096&&p<TARGET_PAGE_ALIGN(start+length)/4096)&&!!(mask&(1u<<c))));
  cases++;
 }
 reset();PGRAPHVkState renderer={0};NV2AState d={.pgraph={&renderer},.vram=&ram,.vram_ptr=ram};renderer.storage_buffers[0].buffer=1;PvideoState s={.base=4080,.in_width=17,.in_height=7,.pitch=48,.format=1};unsigned uploads=0,skips=0;
 /* GPU bypass writes only dirty a live, overlapping cached source. */
 renderer.display.pvideo.upload_addr=4096;
 renderer.display.pvideo.upload_width=16;
 renderer.display.pvideo.upload_height=2;
 renderer.display.pvideo.upload_pitch=32;
 pgraph_vk_mark_pvideo_source_dirty(&d,4096,64);
 assert(!physical_memory_get_dirty_flag(4096,DIRTY_MEMORY_NV2A_PVIDEO));
 renderer.display.pvideo.upload_valid=true;
 pgraph_vk_mark_pvideo_source_dirty(&d,4096,0);
 pgraph_vk_mark_pvideo_source_dirty(&d,0,4096);
 pgraph_vk_mark_pvideo_source_dirty(&d,4160,1);
 assert(!physical_memory_get_dirty_flag(4096,DIRTY_MEMORY_NV2A_PVIDEO));
 pgraph_vk_mark_pvideo_source_dirty(&d,4095,2);
 assert(physical_memory_get_dirty_flag(4096,DIRTY_MEMORY_NV2A_PVIDEO));
 assert(!physical_memory_get_dirty_flag(0,DIRTY_MEMORY_NV2A_PVIDEO));
 reset();
 pgraph_vk_mark_pvideo_source_dirty(&d,4159,2);
 assert(physical_memory_get_dirty_flag(4096,DIRTY_MEMORY_NV2A_PVIDEO));
 reset();renderer.display.pvideo.upload_valid=false;
 for(unsigned frame=0;frame<20000;frame++){
  if(frame%271==0)s.offset=(s.offset==0?11:0);
  if(frame%293==0)s.in_width=(s.in_width==17?18:17);
  if(frame%307==0)s.in_height=(s.in_height==7?9:7);
  if(frame%313==0)s.pitch=(s.pitch==48?64:48);
  if(frame%317==0)s.format^=1;
  if(frame%211==0)renderer.display.pvideo.upload_valid=false;
  hwaddr addr=s.base+s.offset,span=(hwaddr)(s.in_height-1)*s.pitch+((s.in_width+1)/2)*4;
  if(frame%3==0){ram[addr]++;physical_memory_set_dirty_range(addr,1,(uint8_t)all);}
  if(frame%5==0){ram[addr+span-1]++;pgraph_vk_mark_pvideo_source_dirty(&d,addr+span-1,1);}
  /* Other consumers cannot steal this upload's coherence notification. */
  physical_memory_test_and_clear_dirty(addr,span,DIRTY_MEMORY_NV2A_TEX);
  physical_memory_test_and_clear_dirty(addr,span,DIRTY_MEMORY_VGA);
  s.in_s++;s.out_x++;
  uint8_t previous[1024];memcpy(previous,ram+addr,span);
  late_write=(frame%13==0);late_addr=addr+1;
  unsigned old_copies=copies;
  if(frame%101==0)renderer.display.pvideo.width=0;
  upload_pvideo_image(&d.pgraph,s);
  if(copies!=old_copies){assert(copies==old_copies+1);memcpy(image,previous,span);uploads++;}else{assert(!memcmp(image,ram+addr,span));skips++;}

 }
 assert(skips && uploads && uploads+skips==20000);
 assert(begins==uploads&&allocs==(gpu_mode?0:uploads)&&flushes==(gpu_mode?0:uploads)&&copies==uploads&&ends==uploads&&transitions==(gpu_mode?0:uploads*2)&&!in_aux);
 renderer.display.pvideo.upload_valid=false;late_write=false;unsigned old_copies=copies;
 for(unsigned i=0;i<50;i++){s.in_s++;s.out_x++;upload_pvideo_image(&d.pgraph,s);}
 assert(copies==old_copies+1); /* 49 clean/crop-only redraws do no transfer. */
 unsigned before=queries;PvideoState invalid=s;invalid.base=RAM_SIZE+1;assert(pvideo_upload_required(&d,&invalid,false)&&queries==before);invalid=s;invalid.offset=RAM_SIZE;assert(pvideo_upload_required(&d,&invalid,false)&&queries==before);invalid=s;invalid.pitch=INT_MAX;assert(pvideo_upload_required(&d,&invalid,false)&&queries==before);invalid=s;invalid.in_width=0;assert(pvideo_upload_required(&d,&invalid,false)&&queries==before);
 printf(gpu_mode?"GPU cache: PVIDEO dirty clients: %u mask/range/clear cases; 20000 metadata/write-during-copy/reset schedules; %u uploads, %u full conversions/transfers omitted; producer dirtiness and independent consumers passed\n":"PVIDEO dirty clients: %u mask/range/clear cases; 20000 metadata/write-during-copy/reset schedules; %u uploads, %u full conversions/transfers omitted; producer dirtiness and independent consumers passed\n",cases,uploads,skips);
 return 0;
}

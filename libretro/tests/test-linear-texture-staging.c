/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
typedef uint64_t VkDeviceSize;
#define VK_NULL_HANDLE 0
#define VK_IMAGE_ASPECT_COLOR_BIT 1
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
typedef struct {uint64_t bufferOffset;unsigned bufferRowLength,bufferImageHeight;struct {unsigned aspectMask,mipLevel,baseArrayLayer,layerCount;} imageSubresource;struct {int x,y,z;} imageOffset;struct {unsigned width,height,depth;} imageExtent;} VkBufferImageCopy;
typedef struct {unsigned vram_buffer;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;uint8_t *vram_ptr;size_t *vram;} NV2AState;
#define memory_region_size(p) (*(p))
static uint8_t ram[1<<20],arena[1<<20];
static size_t head,copied,reads,calls,row_bytes,source_pitch,source_height;
static const uint8_t *source;
static VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *pg,size_t n,size_t align,uint8_t **out){assert(pg&&align==16&&head+n<=sizeof(arena));size_t offset=(head+15)&~(size_t)15;*out=arena+offset;head=offset+n;calls++;return offset;}
static void *record_copy(void *dst,const void *src,size_t n){
 size_t off=(const uint8_t*)src-source;
 if(n){assert(source_height&&off<source_pitch*source_height);
  if(row_bytes!=source_pitch)assert(off%source_pitch+n<=row_bytes);
  assert(off+n<=(source_height-1)*source_pitch+row_bytes);
 }
 copied+=n;reads++;return memcpy(dst,src,n);
}
#define memcpy record_copy
/* PRODUCTION */
#undef memcpy
static uint32_t seed=0xb8f2601d;
static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
 PGRAPHVkState renderer={0};size_t limit=sizeof(ram);NV2AState d={.pgraph={&renderer},.vram_ptr=ram,.vram=&limit};
 for(unsigned i=0;i<sizeof(ram);i++)ram[i]=(uint8_t)(i*53+(i>>7));
 uint64_t old_bytes=0,new_bytes=0,old_claims=0,new_claims=0;unsigned direct=0,staged=0;
 for(unsigned i=0;i<30000;i++){
  unsigned bpp=1u<<(random32()%3),w=1+random32()%128,h=random32()%34;
  unsigned pitch=(w+random32()%257)*bpp;
  if(i%11==0)pitch=w*bpp;
  unsigned off=random32()%32;renderer.vram_buffer=random32()%2;
  source=ram+off;row_bytes=(size_t)w*bpp;source_pitch=pitch;source_height=h;
  head=copied=reads=calls=0;memset(arena,0xE7,sizeof(arena));
  TextureLevel level={0};stage_linear_2d(&d.pgraph,&level,source,w,h,pitch,bpp);
  bool eligible=renderer.vram_buffer&&off%4==0&&off%bpp==0;
  assert((level.source==LEVEL_VRAM)==eligible&&level.width==w&&level.height==h&&level.depth==1&&!level.crop_offset);
  if(eligible){assert(!calls&&!reads&&!head&&!copied&&level.size==(h?(size_t)(h-1)*pitch+row_bytes:0)&&level.row_length==pitch/bpp);old_claims+=(size_t)pitch*h;new_claims+=level.size;direct++;}
  else{assert(calls==1&&copied==row_bytes*h&&head==copied&&level.size==copied&&!level.row_length);assert(reads==(pitch==row_bytes?1:h));staged++;old_bytes+=(size_t)pitch*h;new_bytes+=copied;}
  VkBufferImageCopy c=texture_level_copy(&level,2,3);
  assert(c.imageSubresource.baseArrayLayer==2&&c.imageSubresource.mipLevel==3&&c.imageSubresource.layerCount==1&&c.imageSubresource.aspectMask==1&&!c.bufferImageHeight&&c.imageExtent.width==w&&c.imageExtent.height==h&&c.imageExtent.depth==1);
  const uint8_t *input=eligible?ram:arena;
  size_t gpu_pitch=(c.bufferRowLength?c.bufferRowLength:w)*bpp;
  for(unsigned y=0;y<h;y++)assert(!memcmp(input+c.bufferOffset+y*gpu_pitch,source+(size_t)y*pitch,row_bytes));
  if(!eligible)for(size_t j=head;j<head+32;j++)assert(arena[j]==0xE7);
 }
 /* Width 16, pitch 4096: only texels, not 4080 padding bytes per row. */
 source=ram;row_bytes=16;source_pitch=4096;source_height=64;renderer.vram_buffer=0;head=copied=reads=calls=0;
 TextureLevel level={0};stage_linear_2d(&d.pgraph,&level,source,16,64,4096,1);assert(level.size==1024&&copied==1024&&head==1024);
 renderer.vram_buffer=1;head=copied=reads=calls=0;
 stage_linear_2d(&d.pgraph,&level,source,16,64,4096,1);
 assert(level.source==LEVEL_VRAM&&level.size==63u*4096+16&&!head&&!copied&&!reads&&!calls);
 printf("Sparse direct claim: 262144 -> %llu bytes; no staging\n",(unsigned long long)level.size);
 printf("Direct claimed bytes: %llu -> %llu; final-row padding excluded\n",(unsigned long long)old_claims,(unsigned long long)new_claims);
 printf("Linear staging: 30000 copy-region/texel/source-read-bound cases; %u direct zero-copy, %u tightly staged; modeled staging bytes %llu -> %llu; sparse-pitch fixture 262144 -> 1024 bytes\n",direct,staged,(unsigned long long)old_bytes,(unsigned long long)new_bytes);return 0;
}

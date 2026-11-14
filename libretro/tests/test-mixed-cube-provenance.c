/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <encodings/xxh3.h>
uint64_t cpu_features_get(void){return 0;}
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 1
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL 2
#define NV2A_PROF_SURF_TO_TEX 0
#define RGBA_GREEN 0
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
typedef unsigned VkCommandBuffer;
typedef struct {unsigned color_format;} TextureShape;
typedef struct {struct {TextureShape state;}key;unsigned current_layout,image,cube_source_mask,cube_source_draw_time[6];int draw_time,validation_frame;uint64_t hash,pixels[6];} TextureBinding;
typedef struct {int draw_time;uint64_t pixel;} SurfaceBinding;
typedef struct {unsigned unused;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;int frame_time;} PGRAPHState;
typedef struct {unsigned vk_format;} VkColorFormatInfo;
static unsigned copies,recordings,ends,transitions;
static VkColorFormatInfo texture_vkf(PGRAPHVkState *r,unsigned f){(void)r;(void)f;return (VkColorFormatInfo){0};}
static void nv2a_profile_inc_counter(unsigned c){assert(c==0);}
static VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg){assert(pg);recordings++;return 1;}
static void pgraph_vk_end_nondraw_commands(PGRAPHState *pg,VkCommandBuffer cmd){assert(pg&&cmd==1);ends++;}
static void pgraph_vk_begin_debug_marker(PGRAPHVkState *r,VkCommandBuffer cmd,unsigned color,const char *name){(void)r;assert(cmd==1&&color==0&&name);}
static void pgraph_vk_end_debug_marker(PGRAPHVkState *r,VkCommandBuffer cmd){(void)r;assert(cmd==1);}
static void pgraph_vk_transition_image_layout(PGRAPHState *pg,VkCommandBuffer cmd,unsigned image,unsigned format,unsigned before,unsigned after){assert(pg&&cmd==1&&image==3&&!format);assert(before<=2&&after>=1&&after<=2);transitions++;}
static void record_surface_copy(PGRAPHState *pg,VkCommandBuffer cmd,SurfaceBinding *s,TextureBinding *t,unsigned f){assert(pg&&cmd==1&&f<6);t->pixels[f]=s->pixel;copies++;}
static void record_color_surface_copies(PGRAPHState *pg,VkCommandBuffer cmd,SurfaceBinding *faces[6],TextureBinding *t,unsigned mask){
 for(unsigned f=0;f<6;f++)if(mask&(1u<<f))record_surface_copy(pg,cmd,faces[f],t,f);
}
static uint8_t ram[6*4096];
static size_t face_bytes,hashed_bytes;
static unsigned gpu_faces;
static uint64_t tracked_hash(const uint8_t *data,size_t len,uint64_t seed __attribute__((unused))){
 uintptr_t base=(uintptr_t)ram,addr=(uintptr_t)data;
 if(addr>=base&&addr<base+sizeof(ram)){
  assert(len==face_bytes&&(addr-base)%face_bytes==0);
  assert(!(gpu_faces&(1u<<((addr-base)/face_bytes))));hashed_bytes+=len;
 }
 return xxh3_64(data,len, 0);
}
#define xxh3_64 tracked_hash
static bool replace_next;
static unsigned ram_uploads;
static bool texture_cow_if_referenced(PGRAPHState *pg,TextureBinding *t,bool open){
 (void)pg;(void)open;
 if(!replace_next)return false;
 replace_next=false;t->current_layout=0;memset(t->pixels,0,sizeof(t->pixels));return true;
}
static void upload_mixed_cube_ram(PGRAPHState *pg,int idx,TextureBinding *t,unsigned gpu){
 (void)pg;assert(!idx);ram_uploads++;
 if(!t->current_layout)t->cube_source_mask=0;
 for(unsigned f=0;f<6;f++)if(!(gpu&(1u<<f)))t->pixels[f]=2000+f;
 t->current_layout=1;
}
/* PRODUCTION */
static void test_ram_hash(void){
 unsigned cases=0;
 for(face_bytes=8;face_bytes<=4096;face_bytes*=8)for(gpu_faces=1;gpu_faces<63;gpu_faces++){
  for(unsigned i=0;i<sizeof(ram);i++)ram[i]=(uint8_t)(i*37+11);
  hashed_bytes=0;uint64_t initial=hash_mixed_cube_ram(ram,face_bytes,gpu_faces);
  assert(hashed_bytes==(6-__builtin_popcount(gpu_faces))*face_bytes);
  for(unsigned f=0;f<6;f++){
   size_t points[]={0,face_bytes/2,face_bytes-1};
   for(unsigned p=0;p<3;p++){
    ram[f*face_bytes+points[p]]^=0x5a;
    uint64_t changed=hash_mixed_cube_ram(ram,face_bytes,gpu_faces);
    assert((changed==initial)==((gpu_faces&(1u<<f))!=0));
    ram[f*face_bytes+points[p]]^=0x5a;cases++;
   }
  }
 }
 printf("Mixed cube RAM hash: %u mutation cases; every RAM face byte consumed, GPU spans unread\n",cases);
}
int main(void){
 test_ram_hash();
 /* A GPU-only CoW restore keeps the RAM identity; later dirty validation
  * must not mistake an uncomputed sentinel for the stored content hash. */
 for(unsigned mask=1;mask<63;mask++){
  PGRAPHVkState renderer={0};PGRAPHState graph={.vk_renderer_state=&renderer,.frame_time=2};
  TextureBinding t={.image=3,.current_layout=1,.cube_source_mask=mask,.hash=42,.draw_time=10,.validation_frame=1};
  SurfaceBinding source[6],*faces[6];
  for(unsigned f=0;f<6;f++){
   source[f]=(SurfaceBinding){.draw_time=11,.pixel=1100+f};faces[f]=mask&(1u<<f)?&source[f]:NULL;
   t.cube_source_draw_time[f]=10;
  }
  replace_next=true;ram_uploads=0;
  refresh_cube(&graph,&t,faces,mask,true,false,0,false);
  assert(ram_uploads==1&&t.hash==42);
  for(unsigned f=0;f<6;f++)assert(t.pixels[f]==(faces[f]?source[f].pixel:2000+f));
  refresh_cube(&graph,&t,faces,mask,true,true,42,true);
  assert(ram_uploads==1&&t.hash==42); /* No redundant second upload. */
 }
 for(unsigned old=1;old<63;old++)for(unsigned next=1;next<63;next++){
  TextureBinding t={.cube_source_mask=old,.validation_frame=2};
  PGRAPHState graph={.frame_time=2};
  assert(cube_hash_needed(true,next,&t,&graph,false,true,false,false)==(old!=next));
 }
 puts("Mixed cube COW: 62 RAM identity schedules and 3844 same-frame source-mask hash gates passed");
 PGRAPHVkState r={0};PGRAPHState pg={.vk_renderer_state=&r};unsigned cases=0;uint64_t saved=0;
 for(unsigned old=0;old<64;old++)for(unsigned mask=0;mask<64;mask++)
 for(unsigned changed=0;changed<64;changed++)for(unsigned fresh=0;fresh<2;fresh++)for(unsigned layout=1;layout<=2;layout++){
  TextureBinding t={.image=3,.current_layout=fresh?0:1,.cube_source_mask=old};
  SurfaceBinding source[6],*faces[6];
  for(unsigned f=0;f<6;f++){
   source[f]=(SurfaceBinding){.draw_time=10+((changed>>f)&1),.pixel=1000+f+100*((changed>>f)&1)};
   faces[f]=mask&(1u<<f)?&source[f]:NULL;
   t.cube_source_draw_time[f]=10;
   t.pixels[f]=fresh?0:(old&(1u<<f)?1000+f:99);
  }
  preserve_cube_upload_sources(&t,mask);
  for(unsigned f=0;f<6;f++)if(!(mask&(1u<<f)))t.pixels[f]=2000+f;
  t.current_layout=layout; /* Native upload is readable; scaled blit is transfer-dst. */
  copies=recordings=ends=transitions=0;copy_cube_faces_to_texture(&pg,faces,&t);
  unsigned expected=__builtin_popcount(mask&(fresh?63u:(~old|changed)));
  assert(copies==expected&&t.cube_source_mask==mask&&t.current_layout==1);
  assert(recordings==(unsigned)(expected||layout==2)&&ends==recordings);
  assert(transitions==(expected?0u:(layout==2?1u:0u)));
  for(unsigned f=0;f<6;f++)assert(t.pixels[f]==(faces[f]?source[f].pixel:2000+f));
  saved+=__builtin_popcount(mask)-copies;cases++;
 }
 printf("Mixed cube provenance: %u payload/routing schedules passed; %llu redundant face copies avoided\n",cases,(unsigned long long)saved);
}

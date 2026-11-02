/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 1
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL 2
#define NV2A_PROF_SURF_TO_TEX 0
#define RGBA_GREEN 0
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
typedef unsigned VkCommandBuffer;
typedef struct {unsigned color_format;} TextureShape;
typedef struct {struct {TextureShape state;}key;unsigned current_layout,image,cube_source_mask,cube_source_draw_time[6];int draw_time;uint64_t pixels[6];} TextureBinding;
typedef struct {int draw_time;uint64_t pixel;} SurfaceBinding;
typedef struct {unsigned unused;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
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
/* PRODUCTION */
int main(void){
 PGRAPHVkState r={0};PGRAPHState pg={&r};unsigned cases=0;uint64_t saved=0;
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
  assert(transitions==(expected?(layout==1?2u:1u):(layout==2?1u:0u)));
  for(unsigned f=0;f<6;f++)assert(t.pixels[f]==(faces[f]?source[f].pixel:2000+f));
  saved+=__builtin_popcount(mask)-copies;cases++;
 }
 printf("Mixed cube provenance: %u payload/routing schedules passed; %llu redundant face copies avoided\n",cases,(unsigned long long)saved);
}

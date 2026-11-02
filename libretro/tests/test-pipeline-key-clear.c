/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
/* TYPES */
struct ShaderBinding { ShaderState state; };
typedef struct {
 struct ShaderBinding *shader_binding;bool dyn_vertex_input;
 VkVertexInputBindingDescription vertex_binding_descriptions[16];
 VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
 unsigned num_active_vertex_binding_descriptions,num_active_vertex_attribute_descriptions;
} PGRAPHVkState;
struct PGRAPHState {PGRAPHVkState *vk_renderer_state;};
static void init_render_pass_state(PGRAPHState *pg,RenderPassState *s){(void)pg;s->color_format=VK_FORMAT_R8G8B8A8_UNORM;s->zeta_format=VK_FORMAT_D24_UNORM_S8_UINT;}
static void pipeline_reg_values(PGRAPHState *pg,uint32_t *regs){(void)pg;for(unsigned i=0;i<9;i++)regs[i]=i*13;}
#ifdef CHECKED
static size_t cleared;
static void *count_clear(void *dst,int value,size_t bytes){cleared+=bytes;return memset(dst,value,bytes);}
#define memset count_clear
#endif
/* FUNCTIONS */
#ifdef CHECKED
#undef memset
#endif
static uint32_t rng=0x731abc24;
static uint32_t random_value(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void correctness(void){
 struct ShaderBinding shader;
 for(size_t i=0;i<sizeof(shader);i++)((uint8_t*)&shader)[i]=(uint8_t)random_value();
 PGRAPHVkState r={.shader_binding=&shader};PGRAPHState pg={&r};
 for(unsigned trial=0;trial<100000;trial++){
  struct {uint8_t before[31];PipelineKey key;uint8_t after[17];} a,b;
  memset(&a,0x97,sizeof(a));memset(&b,0x38,sizeof(b));
  r.dyn_vertex_input=trial&1;
  r.num_active_vertex_binding_descriptions=random_value()%17;
  r.num_active_vertex_attribute_descriptions=random_value()%17;
  ((uint8_t*)&shader)[random_value()%sizeof(shader)]=(uint8_t)random_value();
  for(unsigned i=0;i<16;i++){
   r.vertex_binding_descriptions[i]=(VkVertexInputBindingDescription){i,random_value(),random_value()&1};
   r.vertex_attribute_descriptions[i]=(VkVertexInputAttributeDescription){i,random_value()%16,VK_FORMAT_R32_SFLOAT,random_value()};
  }
#ifdef CHECKED
  cleared=0;
#endif
  reference(&pg,&a.key);
#ifdef CHECKED
  assert(cleared==sizeof(PipelineKey));cleared=0;
#endif
  current(&pg,&b.key);
#ifdef CHECKED
  assert(cleared==sizeof(PipelineKey)-sizeof(ShaderState));
#endif
  assert(!memcmp(&a.key,&b.key,sizeof(PipelineKey)));
  assert(!memcmp(&b.key.shader_state,&shader.state,sizeof(ShaderState)));
  for(unsigned i=0;i<31;i++)assert(a.before[i]==0x97&&b.before[i]==0x38);
  for(unsigned i=0;i<17;i++)assert(a.after[i]==0x97&&b.after[i]==0x38);
 }
 puts("100000 whole-key byte, source-state, guard and clear-byte checks passed (dynamic/static input; independent 0..16 binding/attribute counts)");
}
#ifndef CHECKED
static volatile uint32_t sink;
static double run(unsigned which,unsigned dyn,unsigned count){
 struct ShaderBinding shader;memset(&shader,0x37,sizeof(shader));
 PGRAPHVkState r={.shader_binding=&shader,.dyn_vertex_input=dyn,.num_active_vertex_binding_descriptions=count,.num_active_vertex_attribute_descriptions=count};
 memset(r.vertex_binding_descriptions,0x74,sizeof(r.vertex_binding_descriptions));memset(r.vertex_attribute_descriptions,0x15,sizeof(r.vertex_attribute_descriptions));
 PGRAPHState pg={&r};PipelineKey key;
 void(*fn)(PGRAPHState*,PipelineKey*)=which?current:reference;
 struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<2000000;i++)fn(&pg,&key);
 timespec_get(&b,TIME_UTC);sink=((uint8_t*)&key)[0]+((uint8_t*)&key.shader_state)[sizeof(ShaderState)-1];
 return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/2000000;
}
#endif
int main(void){correctness();printf("key=%zu shader=%zu prefix=%zu suffix=%zu\n",sizeof(PipelineKey),sizeof(ShaderState),offsetof(PipelineKey,shader_state),sizeof(PipelineKey)-offsetof(PipelineKey,shader_state)-sizeof(ShaderState));
#ifndef CHECKED
 for(unsigned rep=0;rep<5;rep++)for(unsigned dyn=0;dyn<2;dyn++)for(unsigned count=0;count<=16;count+=8){double t[2];
 for(unsigned k=0;k<2;k++){unsigned w=k^(rep&1);t[w]=run(w,dyn,count);}
 printf("round=%u dynamic=%u count=%u baseline=%.3f candidate=%.3f ns/key\n",rep,dyn,count,t[0],t[1]);
 }
#endif
}

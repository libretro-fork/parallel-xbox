/* SPDX-License-Identifier: GPL-2.0-or-later */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define nv2a_profile_inc_counter(...) ((void)0)
typedef struct Module{unsigned ready_vertex_index;}Module;
typedef struct Binding{struct{Module*module_info;}vsh;}Binding;
typedef struct Renderer{VkCommandBuffer command_buffer;Binding*shader_binding;int num_active_vertex_binding_descriptions,num_active_vertex_attribute_descriptions;VkVertexInputBindingDescription vertex_binding_descriptions[16];VkVertexInputAttributeDescription vertex_attribute_descriptions[16];struct{bool valid;int nb,na;VkVertexInputBindingDescription bind[16];VkVertexInputAttributeDescription attr[16];}vi_last;}PGRAPHVkState;
typedef struct PGRAPHState{PGRAPHVkState*vk_renderer_state;bool clearing;}PGRAPHState;
static unsigned calls,ready_calls;static uint32_t nb,na;static VkVertexInputBindingDescription2EXT binding[16];static VkVertexInputAttributeDescription2EXT attribute[16];
static void pgraph_vk_ready_vertex_input(PGRAPHState*p){(void)p;ready_calls++;}
static void vkCmdSetVertexInputEXT(VkCommandBuffer c,uint32_t n,const VkVertexInputBindingDescription2EXT*b,uint32_t m,const VkVertexInputAttributeDescription2EXT*a){(void)c;assert(n<=16&&m<=16);nb=n;na=m;memcpy(binding,b,n*sizeof(*b));memcpy(attribute,a,m*sizeof(*a));calls++;}
/* PRODUCTION */
int main(void){
 unsigned cases=0;
 for(int n=0;n<=16;n++)for(int m=0;m<=16;m++){
  PGRAPHVkState r={.num_active_vertex_binding_descriptions=n,.num_active_vertex_attribute_descriptions=m};Module module={0};Binding b={.vsh.module_info=&module};r.shader_binding=&b;PGRAPHState p={.vk_renderer_state=&r};calls=ready_calls=0;
  for(unsigned i=0;i<16;i++){r.vertex_binding_descriptions[i]=(VkVertexInputBindingDescription){.binding=i,.stride=256+i*16,.inputRate=i&1?VK_VERTEX_INPUT_RATE_INSTANCE:VK_VERTEX_INPUT_RATE_VERTEX};r.vertex_attribute_descriptions[i]=(VkVertexInputAttributeDescription){.location=i,.binding=n?i%n:0,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.offset=i*16};}
  emit_dynamic_vertex_input(&p);assert(calls==1&&nb==(unsigned)n&&na==(unsigned)m&&r.vi_last.valid);
  for(int i=0;i<n;i++)assert(binding[i].sType==VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT&&binding[i].binding==(unsigned)i&&binding[i].stride==256u+i*16&&binding[i].inputRate==r.vertex_binding_descriptions[i].inputRate&&binding[i].divisor==1);
  for(int i=0;i<m;i++)assert(attribute[i].sType==VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT&&attribute[i].location==(unsigned)i&&attribute[i].binding==r.vertex_attribute_descriptions[i].binding&&attribute[i].format==VK_FORMAT_R32G32B32A32_SFLOAT&&attribute[i].offset==(unsigned)i*16);
  emit_dynamic_vertex_input(&p);assert(calls==1);
  if(n){r.vertex_binding_descriptions[n-1].stride+=16;emit_dynamic_vertex_input(&p);assert(calls==2&&binding[n-1].stride==r.vertex_binding_descriptions[n-1].stride);}
  unsigned old=calls;if(m){r.vertex_attribute_descriptions[m-1].offset+=4;emit_dynamic_vertex_input(&p);assert(calls==old+1&&attribute[m-1].offset==r.vertex_attribute_descriptions[m-1].offset);}
  old=calls;p.clearing=true;emit_dynamic_vertex_input(&p);assert(calls==old&&!r.vi_last.valid);p.clearing=false;emit_dynamic_vertex_input(&p);assert(calls==old+1);
  module.ready_vertex_index=1;old=calls;emit_dynamic_vertex_input(&p);assert(ready_calls==1&&calls==old);
  p.clearing=true;emit_dynamic_vertex_input(&p);assert(ready_calls==1&&calls==old&&!r.vi_last.valid);cases++;
 }
 printf("Shader-object input: %u independent 0..16 binding/attribute, dirty payload/cache/utility/ready-route schedules PASS\n",cases);return 0;
}

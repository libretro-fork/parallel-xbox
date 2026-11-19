/* SPDX-License-Identifier: GPL-2.0-or-later */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/blend.h"
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define ctz32(v) __builtin_ctz(v)
typedef struct PGRAPHVkState {VkCommandBuffer command_buffer;void*shader_binding;bool dyn_cull_front,dyn_blend;struct{bool draw;VkPhysicalDeviceFeatures features;}shader_objects;struct{bool valid;uint32_t blend,blendcolor,control_0,control_1,control_2,topology,setupraster;}dyn_last;}PGRAPHVkState;
typedef struct PGRAPHState{PGRAPHVkState*vk_renderer_state;bool clearing;struct{unsigned color_format;}surface_shape;uint32_t regs[0x2000];}PGRAPHState;
static uint32_t pgraph_reg_r(PGRAPHState*p,unsigned n){assert(n<ARRAY_SIZE(p->regs));return p->regs[n];}
static VkPrimitiveTopology get_primitive_topology(PGRAPHState*p){(void)p;return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;}
static void pgraph_argb_pack32_to_rgba_float(uint32_t p,float*c){c[0]=((p>>16)&255)/255.f;c[1]=((p>>8)&255)/255.f;c[2]=(p&255)/255.f;c[3]=(p>>24)/255.f;}
/* MAPS */
static struct Capture{VkCullModeFlags cull;VkFrontFace front;VkColorComponentFlags write;VkBool32 blend,depth,depthwrite,bounds,stencil;VkCompareOp compare,scompare;VkStencilOp fail,pass,zfail;uint32_t readmask,writemask,reference;VkPrimitiveTopology topology;VkColorBlendEquationEXT eq;float constants[4];}cap;
static unsigned calls;static uint64_t seen;
#define ONE(name,type,field,bit) static void name(VkCommandBuffer c,type v){(void)c;cap.field=v;calls++;seen|=1ull<<(bit);}
ONE(vkCmdSetCullModeEXT,VkCullModeFlags,cull,0)
ONE(vkCmdSetFrontFaceEXT,VkFrontFace,front,1)
ONE(vkCmdSetDepthTestEnableEXT,VkBool32,depth,2)
ONE(vkCmdSetDepthWriteEnableEXT,VkBool32,depthwrite,3)
ONE(vkCmdSetDepthCompareOpEXT,VkCompareOp,compare,4)
ONE(vkCmdSetDepthBoundsTestEnableEXT,VkBool32,bounds,5)
ONE(vkCmdSetStencilTestEnableEXT,VkBool32,stencil,6)
ONE(vkCmdSetPrimitiveTopologyEXT,VkPrimitiveTopology,topology,7)
static void vkCmdSetBlendConstants(VkCommandBuffer c,const float*v){(void)c;memcpy(cap.constants,v,sizeof(cap.constants));calls++;seen|=1ull<<8;}
static void vkCmdSetColorWriteMaskEXT(VkCommandBuffer c,uint32_t first,uint32_t n,const VkColorComponentFlags*v){(void)c;assert(first==0&&n==1);cap.write=*v;calls++;seen|=1ull<<9;}
static void vkCmdSetColorBlendEnableEXT(VkCommandBuffer c,uint32_t first,uint32_t n,const VkBool32*v){(void)c;assert(first==0&&n==1);cap.blend=*v;calls++;seen|=1ull<<10;}
static void vkCmdSetColorBlendEquationEXT(VkCommandBuffer c,uint32_t first,uint32_t n,const VkColorBlendEquationEXT*v){(void)c;assert(first==0&&n==1);cap.eq=*v;calls++;seen|=1ull<<11;}
static void vkCmdSetStencilOpEXT(VkCommandBuffer c,VkStencilFaceFlags face,VkStencilOp f,VkStencilOp p,VkStencilOp z,VkCompareOp compare){(void)c;assert(face==VK_STENCIL_FACE_FRONT_AND_BACK);cap.fail=f;cap.pass=p;cap.zfail=z;cap.scompare=compare;calls++;seen|=1ull<<12;}
#define STENCIL(name,field,bit) static void name(VkCommandBuffer c,VkStencilFaceFlags f,uint32_t v){(void)c;assert(f==VK_STENCIL_FACE_FRONT_AND_BACK);cap.field=v;calls++;seen|=1ull<<(bit);}
STENCIL(vkCmdSetStencilCompareMask,readmask,13)
STENCIL(vkCmdSetStencilWriteMask,writemask,14)
STENCIL(vkCmdSetStencilReference,reference,15)
/* PRODUCTION */
static uint32_t put(uint32_t v,uint32_t mask){return (v<<__builtin_ctz(mask))&mask;}
int main(void){
 static PGRAPHState p;PGRAPHVkState r={0};p.vk_renderer_state=&r;r.shader_objects.draw=true;r.shader_binding=&p;p.surface_shape.color_format=8;
 unsigned cases=0;
 for(unsigned flags=0;flags<64;flags++)for(unsigned z=0;z<8;z++)for(unsigned op=1;op<=8;op++)for(unsigned sf=0;sf<16;sf++){
  if(sf==11){continue;}
  unsigned df=(sf+3)%16;if(df==11)df=1;unsigned equation=sf%7;
  uint32_t c0=put(z,NV_PGRAPH_CONTROL_0_ZFUNC)|((flags&1)?NV_PGRAPH_CONTROL_0_ZENABLE:0)|((flags&2)?NV_PGRAPH_CONTROL_0_ZWRITEENABLE:0)|NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE|NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE;
  uint32_t c1=put(7-z,NV_PGRAPH_CONTROL_1_STENCIL_FUNC)|put(0xa3,NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ)|put(0x5e,NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE)|put(0x72,NV_PGRAPH_CONTROL_1_STENCIL_REF)|((flags&4)?NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE:0);
  uint32_t c2=put(op,NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL)|put(9-op,NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL)|put(op,NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS);
  unsigned cull=(flags>>4)&3;uint32_t setup=put(cull,NV_PGRAPH_SETUPRASTER_CULLCTRL)|((flags&8)?NV_PGRAPH_SETUPRASTER_CULLENABLE:0)|((flags&16)?NV_PGRAPH_SETUPRASTER_FRONTFACE:0);
  p.regs[NV_PGRAPH_CONTROL_0]=c0;p.regs[NV_PGRAPH_CONTROL_1]=c1;p.regs[NV_PGRAPH_CONTROL_2]=c2;p.regs[NV_PGRAPH_SETUPRASTER]=setup;p.regs[NV_PGRAPH_BLENDCOLOR]=0xa35e7219;
  p.regs[NV_PGRAPH_BLEND]=put(sf,NV_PGRAPH_BLEND_SFACTOR)|put(df,NV_PGRAPH_BLEND_DFACTOR)|put(equation,NV_PGRAPH_BLEND_EQN)|((flags&32)?NV_PGRAPH_BLEND_EN:0);
  r.shader_objects.features.depthBounds=flags&1;r.dyn_blend=r.dyn_cull_front=false;memset(&cap,0,sizeof(cap));seen=0;calls=0;emit_dynamic_state(&p,true);
  assert(seen==(0xffffull&~((flags&1)?0:1ull<<5)));
  assert(cap.cull==((flags&8)?pgraph_cull_face_vk_map[cull]:VK_CULL_MODE_NONE)&&cap.front==((flags&16)?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE));
  assert(cap.write==(VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_B_BIT)&&cap.blend==!!(flags&32)&&cap.depth==!!(flags&1)&&cap.depthwrite==!!(flags&2)&&cap.compare==(VkCompareOp)z&&cap.stencil==!!(flags&4));
  assert(cap.eq.srcColorBlendFactor==pgraph_blend_factor_vk_map[sf]&&cap.eq.dstColorBlendFactor==pgraph_blend_factor_vk_map[df]&&cap.eq.colorBlendOp==pgraph_blend_equation_vk_map[equation]);
  assert(cap.eq.srcAlphaBlendFactor==cap.eq.srcColorBlendFactor&&cap.eq.dstAlphaBlendFactor==cap.eq.dstColorBlendFactor&&cap.eq.alphaBlendOp==cap.eq.colorBlendOp);
  assert(cap.readmask==0xa3&&cap.writemask==0x5e&&cap.reference==0x72&&cap.topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
  assert(cap.fail==((flags&4)?pgraph_stencil_op_vk_map[op]:VK_STENCIL_OP_KEEP)&&cap.zfail==((flags&4)?pgraph_stencil_op_vk_map[9-op]:VK_STENCIL_OP_KEEP)&&cap.pass==((flags&4)?pgraph_stencil_op_vk_map[op]:VK_STENCIL_OP_KEEP)&&cap.scompare==((flags&4)?(VkCompareOp)(7-z):VK_COMPARE_OP_ALWAYS));
  unsigned before=calls;emit_dynamic_state(&p,false);assert(calls==before);
  p.regs[NV_PGRAPH_CONTROL_0]^=NV_PGRAPH_CONTROL_0_ZWRITEENABLE;
  emit_dynamic_state(&p,false);assert(calls==before*2&&cap.depthwrite==!(flags&2));
  p.regs[NV_PGRAPH_CONTROL_0]^=NV_PGRAPH_CONTROL_0_ZWRITEENABLE;
  emit_dynamic_state(&p,false);assert(calls==before*3&&cap.depthwrite==!!(flags&2));
  emit_dynamic_state(&p,true);assert(calls==before*4);
  p.clearing=true;emit_dynamic_state(&p,true);assert(calls==before*4&&!r.dyn_last.valid);p.clearing=false;cases++;
 }
 printf("Shader-object dynamic state: %u actual-emitter depth/stencil/blend/cull/caching/utility schedules PASS\n",cases);return 0;
}

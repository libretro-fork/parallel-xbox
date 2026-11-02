/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "qemu/queue.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
typedef struct ShaderModuleInfo {VkShaderModule module;} ShaderModuleInfo;
/* TYPES */
typedef struct {
 bool pipeline_library,dyn_cull_front,dyn_vertex_input;
 VkDevice device;VkPipelineCache vk_pipeline_cache;void *color_binding;
 ShaderBinding *shader_binding;
} PGRAPHVkState;
struct PGRAPHState {PGRAPHVkState *vk_renderer_state;int primitive_mode;bool program_data_dirty;};
typedef struct {PGRAPHState pgraph;} NV2AState;
typedef struct {uint8_t hash_bytes[2048];size_t hash_size;uint64_t hash,words[256];unsigned n,gets,refs,unrefs,layouts,destroys,lookups,stores,creates;} Log;
static Log log_data;
static ShaderState requested;
static ShaderModuleInfo modules[2];
static bool geometry,cache_hit;
static size_t cleared;
static void record(uint64_t v){assert(log_data.n<256);log_data.words[log_data.n++]=v;}
static uint64_t fast_hash(const uint8_t *p,size_t n){
 assert(n<=sizeof(log_data.hash_bytes));memcpy(log_data.hash_bytes,p,n);log_data.hash_size=n;
 uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;i++)h=(h^p[i])*1099511628211ull;log_data.hash=h;return h;
}
ShaderState pgraph_glsl_get_shader_state(PGRAPHState *pg){log_data.gets++;record(pg->primitive_mode);pg->program_data_dirty=false;return requested;}
static void pgraph_vk_ref_state_modules(PGRAPHVkState *r,const ShaderState *s,ShaderModuleInfo **geom,ShaderModuleInfo **vsh,ShaderModuleInfo **psh){
 (void)r;assert(!memcmp(s,&requested,sizeof(*s))&&!psh);*vsh=&modules[0];*geom=geometry?&modules[1]:NULL;log_data.refs+=1+geometry;
}
static void pgraph_vk_unref_shader_module(PGRAPHVkState *r,ShaderModuleInfo *m){(void)r;assert(m==&modules[0]||m==&modules[1]);log_data.unrefs++;}
static void ahead_pre_state(const ShaderState *s,VkPipelineRasterizationStateCreateInfo *r,VkPipelineInputAssemblyStateCreateInfo *ia){
 (void)s;memset(r,0,sizeof(*r));r->sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;r->lineWidth=1;
 memset(ia,0,sizeof(*ia));ia->sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;ia->topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
}
static int ahead_dynamic_states(PGRAPHVkState *r,const ShaderState *s,VkDynamicState *out){(void)r;(void)s;out[0]=VK_DYNAMIC_STATE_VIEWPORT;return 1;}
static VkPipelineLayout create_pipeline_layout(PGRAPHVkState *r,const ShaderState *s){(void)r;(void)s;log_data.layouts++;return (VkPipelineLayout)(uintptr_t)17;}
static void vkDestroyPipelineLayout(VkDevice d,VkPipelineLayout l,const VkAllocationCallbacks *a){(void)d;assert((uintptr_t)l==17&&!a);log_data.destroys++;}
static void init_render_pass_state(PGRAPHState *pg,RenderPassState *rp){(void)pg;rp->color_format=VK_FORMAT_R8G8B8A8_UNORM;rp->zeta_format=VK_FORMAT_D24_UNORM_S8_UINT;}
static VkRenderPass get_render_pass(PGRAPHVkState *r,RenderPassState *rp){(void)r;assert(rp->color_format==VK_FORMAT_R8G8B8A8_UNORM);return (VkRenderPass)(uintptr_t)19;}
static VkPipeline gpl_lookup(PGRAPHVkState *r,bool frag,uint64_t h,bool *found){(void)r;assert(!frag&&h==log_data.hash);log_data.lookups++;*found=cache_hit;return cache_hit?(VkPipeline)(uintptr_t)23:VK_NULL_HANDLE;}
static void gpl_store(PGRAPHVkState *r,bool frag,uint64_t h,VkPipeline p){(void)r;assert(!frag&&h==log_data.hash&&(uintptr_t)p==29);log_data.stores++;}
static VkResult vkCreateGraphicsPipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo *ci,const VkAllocationCallbacks *alloc,VkPipeline *out){
 (void)d;(void)cache;assert(count==1&&!alloc);log_data.creates++;
 const VkGraphicsPipelineLibraryCreateInfoEXT *lib=ci->pNext;
 record(ci->sType);record(ci->flags);record(lib->sType);record(lib->flags);record(ci->stageCount);
 record((uintptr_t)ci->layout);record((uintptr_t)ci->renderPass);record(ci->subpass);record((uintptr_t)ci->basePipelineHandle);record(ci->basePipelineIndex);
 assert(!lib->pNext&&!ci->pTessellationState&&!ci->pMultisampleState&&!ci->pDepthStencilState&&!ci->pColorBlendState);
 for(unsigned i=0;i<ci->stageCount;i++){const VkPipelineShaderStageCreateInfo *s=&ci->pStages[i];assert(!s->pNext&&!s->pSpecializationInfo&&!strcmp(s->pName,"main"));record(s->sType);record(s->flags);record(s->stage);record((uintptr_t)s->module);}
 const VkPipelineVertexInputStateCreateInfo *vi=ci->pVertexInputState;record(vi->sType);record(vi->flags);record(vi->vertexBindingDescriptionCount);record(vi->vertexAttributeDescriptionCount);
 assert(!vi->pNext);
 for(unsigned i=0;i<vi->vertexBindingDescriptionCount;i++){record(vi->pVertexBindingDescriptions[i].binding);record(vi->pVertexBindingDescriptions[i].stride);record(vi->pVertexBindingDescriptions[i].inputRate);}
 for(unsigned i=0;i<vi->vertexAttributeDescriptionCount;i++){record(vi->pVertexAttributeDescriptions[i].location);record(vi->pVertexAttributeDescriptions[i].binding);record(vi->pVertexAttributeDescriptions[i].format);record(vi->pVertexAttributeDescriptions[i].offset);}
 const VkPipelineInputAssemblyStateCreateInfo *ia=ci->pInputAssemblyState;record(ia->sType);record(ia->flags);record(ia->topology);record(ia->primitiveRestartEnable);assert(!ia->pNext);
 const VkPipelineRasterizationStateCreateInfo *rs=ci->pRasterizationState;
 record(rs->sType);record(rs->flags);record(rs->depthClampEnable);record(rs->rasterizerDiscardEnable);record(rs->polygonMode);record(rs->cullMode);record(rs->frontFace);record(rs->depthBiasEnable);assert(!rs->pNext&&rs->lineWidth==1&&rs->depthBiasConstantFactor==0&&rs->depthBiasClamp==0&&rs->depthBiasSlopeFactor==0);
 const VkPipelineViewportStateCreateInfo *vp=ci->pViewportState;record(vp->sType);record(vp->flags);record(vp->viewportCount);record(vp->scissorCount);assert(!vp->pNext&&!vp->pViewports&&!vp->pScissors);
 const VkPipelineDynamicStateCreateInfo *dy=ci->pDynamicState;record(dy->sType);record(dy->flags);record(dy->dynamicStateCount);assert(!dy->pNext);for(unsigned i=0;i<dy->dynamicStateCount;i++)record(dy->pDynamicStates[i]);
 *out=(VkPipeline)(uintptr_t)29;return VK_SUCCESS;
}
#define VK_CHECK(expr) assert((expr)==VK_SUCCESS)
#ifdef CHECKED
static void *count_clear(void *dst,int v,size_t n){cleared+=n;return memset(dst,v,n);}
#define memset count_clear
#endif
/* FUNCTIONS */
#ifdef CHECKED
#undef memset
#endif
static void correctness(void){
 ShaderBinding binding;memset(&binding,0,sizeof(binding));ShaderModuleInfo unrelated={(VkShaderModule)(uintptr_t)99};binding.vsh.module_info=&unrelated;
 unsigned cases=0;
 for(unsigned serial=0;serial<64;serial++)for(unsigned mask=0;mask<256;mask++){
  PGRAPHVkState r={.pipeline_library=mask&1,.dyn_cull_front=mask&2,.dyn_vertex_input=mask&4,.device=(VkDevice)(uintptr_t)(mask&8),.color_binding=(void*)(uintptr_t)(mask&16),.shader_binding=&binding};
  NV2AState d={.pgraph={&r,serial&1?PRIM_TYPE_INVALID:PRIM_TYPE_LINES,(serial&2)!=0}};
  r.shader_binding=serial&4?NULL:&binding;
  ShaderBinding *saved_binding=r.shader_binding;
  PGRAPHState saved=d.pgraph;memset(&requested,0,sizeof(requested));requested.vsh.is_fixed_function=(mask&32)!=0;
  geometry=(mask&64)!=0;cache_hit=(mask&128)!=0;modules[0].module=(VkShaderModule)(uintptr_t)(100+serial);modules[1].module=(VkShaderModule)(uintptr_t)(200+serial);
  memset(&log_data,0,sizeof(log_data));cleared=0;reference_prewarm(&d);Log expected;memcpy(&expected,&log_data,sizeof(expected));size_t old_clear=cleared;
  assert(r.shader_binding==saved_binding&&d.pgraph.primitive_mode==saved.primitive_mode&&d.pgraph.program_data_dirty==saved.program_data_dirty);
  memset(&log_data,0,sizeof(log_data));cleared=0;pgraph_vk_prewarm_shaders(&d);
  assert(!memcmp(&log_data,&expected,sizeof(expected)));
  assert(r.shader_binding==saved_binding&&d.pgraph.primitive_mode==saved.primitive_mode&&d.pgraph.program_data_dirty==saved.program_data_dirty);
  bool active=(mask&31)==31&&!(mask&32);
  assert(log_data.refs==(active?1u+geometry:0u)&&log_data.refs==log_data.unrefs);
  assert(log_data.layouts==active&&log_data.destroys==active&&log_data.lookups==active);
  assert(log_data.creates==(active&&!cache_hit)&&log_data.stores==log_data.creates);
#ifdef CHECKED
  assert(old_clear-cleared==(active?sizeof(ShaderBinding)+sizeof(PipelineKey):0));
#else
  (void)old_clear;
#endif
  cases++;
 }
 NV2AState none={0};memset(&log_data,0,sizeof(log_data));pgraph_vk_prewarm_shaders(&none);assert(!log_data.gets);
 printf("%u prewarm cases: raw hash-key bytes, driver-input captures, module refs, gate policy and restored caller state match\n",cases);
}
static void draw_correctness(void){
 ShaderBinding binding;memset(&binding,0,sizeof(binding));binding.vsh.module_info=&modules[0];
 unsigned cases=0;
 for(unsigned serial=0;serial<64;serial++)for(unsigned flags=0;flags<16;flags++)for(unsigned count=0;count<=16;count++){
  geometry=(flags&4)!=0;cache_hit=(flags&8)!=0;binding.geom.module_info=geometry?&modules[1]:NULL;
  modules[0].module=(VkShaderModule)(uintptr_t)(100+serial);modules[1].module=(VkShaderModule)(uintptr_t)(200+serial);
  PGRAPHVkState r={.dyn_cull_front=(flags&1)!=0,.dyn_vertex_input=(flags&2)!=0,.shader_binding=&binding};PGRAPHState pg={.vk_renderer_state=&r};
  PipelineKey key;memset(&key,0,sizeof(key));key.render_pass_state=(RenderPassState){VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_D24_UNORM_S8_UINT};
  VkPipelineShaderStageCreateInfo stages[3];int n=pre_stages(stages,&modules[0],binding.geom.module_info);
  stages[n]=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=(VkShaderModule)(uintptr_t)999,.pName="main"};
  VkVertexInputBindingDescription binds[16];VkVertexInputAttributeDescription attrs[16];
  for(unsigned i=0;i<16;i++){binds[i]=(VkVertexInputBindingDescription){i,serial+i,VK_VERTEX_INPUT_RATE_VERTEX};attrs[i]=(VkVertexInputAttributeDescription){i,i,VK_FORMAT_R32_SFLOAT,serial+4*i};}
  VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=count,.pVertexBindingDescriptions=count?binds:NULL,.vertexAttributeDescriptionCount=16-count,.pVertexAttributeDescriptions=count<16?attrs:NULL};
  VkPipelineRasterizationStateCreateInfo raster;VkPipelineInputAssemblyStateCreateInfo ia;ahead_pre_state(&requested,&raster,&ia);raster.polygonMode=serial%3;raster.cullMode=serial&3;ia.topology=serial%11;
  VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
  VkDynamicState value=VK_DYNAMIC_STATE_VIEWPORT;VkPipelineDynamicStateCreateInfo dyn={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=1,.pDynamicStates=&value};
  VkGraphicsPipelineCreateInfo full={.stageCount=n+1,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pRasterizationState=&raster,.pViewportState=&vp,.pDynamicState=&dyn,.renderPass=(VkRenderPass)(uintptr_t)19};
  memset(&log_data,0,sizeof(log_data));VkPipeline old=reference_get_pre(&pg,&full,&key,(VkPipelineLayout)(uintptr_t)17,n);Log expected;memcpy(&expected,&log_data,sizeof(expected));
  memset(&log_data,0,sizeof(log_data));VkPipeline now=gpl_get_pre(&pg,&full,&key.render_pass_state,(VkPipelineLayout)(uintptr_t)17,n);
  assert(old==now&&!memcmp(&expected,&log_data,sizeof(expected)));cases++;
 }
 printf("%u draw-helper cases: static/dynamic state, 0..16 vertex descriptions, optional geometry, cache hit/miss, fragment-stage exclusion match\n",cases);
}
#ifndef CHECKED
static volatile uint64_t sink;
static double bench(bool current,unsigned mode){
 ShaderBinding binding;memset(&binding,0,sizeof(binding));
 PGRAPHVkState r={.pipeline_library=true,.dyn_cull_front=true,.dyn_vertex_input=true,.device=(VkDevice)(uintptr_t)1,.color_binding=(void*)(uintptr_t)1,.shader_binding=&binding};
 NV2AState d={.pgraph={&r,PRIM_TYPE_TRIANGLES,true}};
 memset(&requested,0,sizeof(requested));geometry=(mode&1)!=0;cache_hit=(mode&2)!=0;
 modules[0].module=(VkShaderModule)(uintptr_t)101;modules[1].module=(VkShaderModule)(uintptr_t)202;
 void(*run)(NV2AState*)=current?pgraph_vk_prewarm_shaders:reference_prewarm;
 struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<100000;i++){memset(&log_data,0,sizeof(log_data));run(&d);}
 timespec_get(&b,TIME_UTC);sink=log_data.hash+log_data.creates;
 return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/100000;
}
#endif
int main(void){correctness();draw_correctness();printf("eliminated binding=%zu and key=%zu bytes of explicit temporary clearing per active prewarm\n",sizeof(ShaderBinding),sizeof(PipelineKey));
#ifndef CHECKED
 puts("Modeled prewarm including hash and driver-input capture; no GPU/driver compile; ns/call; geometry=mode&1, hit=mode&2");
 for(unsigned round=0;round<5;round++)for(unsigned mode=0;mode<4;mode++){
  double t[2];for(unsigned k=0;k<2;k++){unsigned w=k^(round&1);t[w]=bench(w,mode);}
  printf("round=%u mode=%u baseline=%.3f current=%.3f ns/call\n",round,mode,t[0],t[1]);
 }
#endif
}

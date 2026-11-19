/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "hw/xbox/nv2a/pgraph/vk/glsl.h"
#include "hw/xbox/nv2a/pgraph/spv/vsh.h"
#include "hw/xbox/nv2a/pgraph/gl/ready-ff-state.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define ROUND_UP(a,b) (((a)+(b)-1)/(b)*(b))
#define VSH_UBO_BINDING 0
#define BUFFER_UNIFORM 0
#define BUFFER_UNIFORM_STAGING 1

typedef struct LruNode{unsigned unused;}LruNode;
typedef struct ShaderModuleInfo{
 unsigned refcnt;uint16_t ready_vertex_index;bool uniform_staged_valid;
 struct ShaderModuleInfo *object_gpu_next;uint32_t object_retire_after;bool object_gpu_ref;
 VkShaderModule module;VkShaderEXT shader_object;
 uint64_t uniform_staged_epoch;VkDeviceSize uniform_staged_offset;
 ShaderUniformLayout uniforms;
}ShaderModuleInfo;
/* TYPES */
typedef struct ShaderBinding{ShaderState state;struct{ShaderModuleInfo *module_info;}vsh;}ShaderBinding;
typedef struct StorageBuffer{VkBuffer buffer;VkDeviceSize buffer_offset,buffer_size;uint8_t *mapped;}StorageBuffer;
typedef struct PGRAPHVkState{
 bool dyn_vertex_input,dyn_cull_front,dyn_blend;
 struct{bool enabled;ShaderModuleInfo *gpu_owners;}shader_objects;
 uint32_t submit_count,settled_submit;bool in_command_buffer;
 VkPhysicalDeviceProperties device_props;
 /* BANK */
 unsigned shader_module_cache;
 int num_active_vertex_binding_descriptions,num_active_vertex_attribute_descriptions;
 VkVertexInputBindingDescription vertex_binding_descriptions[16];
 VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
 struct{bool valid;int nb,na;VkVertexInputBindingDescription bind[16];VkVertexInputAttributeDescription attr[16];}vi_last;
 StorageBuffer storage_buffers[2];
 uint64_t uniform_upload_epoch;
 VkDeviceSize uniform_buffer_offsets[2];VkCommandBuffer command_buffer;
 ShaderBinding *shader_binding;
}PGRAPHVkState;
struct PGRAPHState{PGRAPHVkState *vk_renderer_state;uint16_t uniform_attrs;struct{float inline_value[4];}vertex_attributes[16];};
static bool option=true,pgraph_vk_preserve_float32=true,program_supported=true;
static int fail_at=-1;static unsigned malformed_layout;static unsigned made,freed,returned,source_live,source_count;
static VkDeviceSize bound_offset;static unsigned bound_slot,input_calls;static VkVertexInputBindingDescription2EXT captured_bindings[16];static VkVertexInputAttributeDescription2EXT captured_attrs[16];
static bool pgraph_vk_prepared_vertex_enabled(void){return option;}
static void lru_add_free(unsigned *cache,LruNode *node){(void)cache;(void)node;returned++;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m){assert(m);m->refcnt++;}
static void pgraph_vk_unref_shader_module(PGRAPHVkState *r,ShaderModuleInfo *m){(void)r;assert(m&&m->refcnt);if(!--m->refcnt){assert(!m->object_gpu_ref);free(m->uniforms.allocation);free((void*)m->uniforms.uniforms);free(m);freed++;}}
struct MString{VshState state;GenVshGlslOptions opts;bool interpreter;};
MString *pgraph_glsl_gen_vsh(const VshState *s,GenVshGlslOptions opts){MString *p=malloc(sizeof(*p));assert(p);p->state=*s;p->opts=opts;p->interpreter=false;source_live++;source_count++;return p;}
MString *pgraph_glsl_gen_vsh_interpreter_dynamic(const VshState *s,GenVshGlslOptions opts){MString *p=pgraph_glsl_gen_vsh(s,opts);p->interpreter=true;return p;}
static const char *mstring_get_str(MString *s){return (const char*)s;}
static void mstring_unref(MString *s){assert(source_live);source_live--;free(s);}
bool pgraph_spv_vsh_supported(const VshState *s,SpvVshOptions opts){(void)s;assert(opts.preserve_float32);return program_supported;}
static ShaderModuleInfo *pgraph_vk_create_optional_vertex_module(PGRAPHVkState *r,const char *text,bool preserve_float32){
 (void)r;const MString *source=(const MString*)text;assert(source->opts.vulkan&&source->opts.locations&&!source->opts.use_push_constants_for_uniform_attrs);
 unsigned native_index=source_count-1,index=native_index*(TEST_PREFIX_ALIASES?2u:1u);assert(!TEST_PREFIX_ALIASES||!source->opts.prefix_outputs);assert(preserve_float32==(index>=8||!!(index&4)));if((int)native_index==fail_at)return NULL;
 ShaderModuleInfo *m=calloc(1,sizeof(*m));assert(m);made++;m->shader_object=(VkShaderEXT)(uintptr_t)(index+1);
 size_t size=source->interpreter?2240:255;m->uniforms.total_size=size;m->uniforms.allocation=calloc(1,size);assert(m->uniforms.allocation);
 if(source->interpreter){ShaderUniform *u=calloc(2,sizeof(*u));assert(u);u[0]=(ShaderUniform){.name="vpCode",.dim_v=4,.dim_a=136,.stride=16,.offset=16};u[1]=(ShaderUniform){.name="vpFlags",.dim_v=1,.dim_a=1,.offset=2192};if(malformed_layout==1)u[0].dim_a=135;
  if(malformed_layout==2)u[0].dim_v=3;
  if(malformed_layout==3)u[1].offset=size;
  if(malformed_layout==4)u[1].dim_a=2;
  if(malformed_layout==5)u[0].offset=128;
  m->uniforms.uniforms=u;m->uniforms.num_uniforms=2;}
 return m;
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer cmd,uint32_t first,uint32_t count,const VkBuffer *b,const VkDeviceSize *offsets){(void)cmd;assert(count==1&&b[0]==(VkBuffer)(uintptr_t)1234);bound_slot=first;bound_offset=*offsets;}
VKAPI_ATTR void VKAPI_CALL vkCmdSetVertexInputEXT(VkCommandBuffer cmd,uint32_t nb,const VkVertexInputBindingDescription2EXT *b,uint32_t na,const VkVertexInputAttributeDescription2EXT *a){(void)cmd;assert(nb<=16&&na==16);input_calls++;memcpy(captured_bindings,b,nb*sizeof(*b));memcpy(captured_attrs,a,na*sizeof(*a));}
static bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg,int index,VkDeviceSize size,VkDeviceAddress alignment){PGRAPHVkState *r=pg->vk_renderer_state;return ROUND_UP(r->storage_buffers[index].buffer_offset,alignment)+size<=r->storage_buffers[index].buffer_size;}
/* PRODUCTION */
/* SNAPSHOT AND RESERVATION */
/* GPU OWNER FUNCTIONS */
static PGRAPHVkState fresh(void){PGRAPHVkState r={.dyn_vertex_input=true,.dyn_cull_front=true,.dyn_blend=true,.shader_objects={.enabled=true}};r.device_props.limits.maxVertexInputAttributes=r.device_props.limits.maxVertexInputBindings=16;r.device_props.limits.maxUniformBufferRange=65536;return r;}
static void stages(void){
 const unsigned native_count=TEST_PREFIX_ALIASES?60u:120u;
 for(int failure=-1;failure<(int)native_count;failure++){
  PGRAPHVkState r=fresh();ShaderModuleCacheEntry *pool=calloc(120,sizeof(*pool));assert(pool);r.ready_vertex.stages=pool;made=freed=returned=source_count=source_live=0;fail_at=failure;
  vk_ready_vertex_init(&r);assert(!source_live);
  if(failure>=0){assert(!r.ready_vertex.ready&&!r.ready_vertex.stages&&made==freed&&returned==120);}
  else{assert(r.ready_vertex.ready&&made==native_count&&!freed&&source_count==native_count);for(unsigned i=0;i<120;i++){assert(pool[i].key.native_kind==1);assert(pool[i].key.vsh.glsl_opts.prefix_outputs==!!(i&1u));assert(pool[i].module_info->shader_object&&!pool[i].module_info->module);assert(pool[i].key.vsh.ready_version==VK_READY_VERTEX_VERSION);assert(pool[i].key.vsh.ready_kind==(i<8?VK_READY_VERTEX_PROGRAMMABLE:VK_READY_VERTEX_FF));unsigned canonical=TEST_PREFIX_ALIASES?(i&~1u):i;assert(pool[i].module_info->ready_vertex_index==canonical+1);if(TEST_PREFIX_ALIASES){assert(pool[i].module_info==pool[canonical].module_info&&pool[i].module_info->refcnt==2);assert(!r.ready_vertex.payload[i|1u].valid);assert(!r.ready_vertex.payload[i|1u].code_loc&&!r.ready_vertex.payload[i|1u].flags_loc);}}
   ShaderModuleInfo *borrow=pool[3].module_info;pgraph_vk_ref_shader_module(borrow);r.in_command_buffer=true;r.submit_count=r.settled_submit=16;pgraph_vk_shader_object_use(&r,borrow);assert(borrow->object_gpu_ref&&r.shader_objects.gpu_owners==borrow);vk_ready_vertex_finalize(&r);assert(freed==native_count-1&&borrow->refcnt==2);pgraph_vk_shader_objects_retire(&r);assert(borrow->refcnt==2&&freed==native_count-1);pgraph_vk_unref_shader_module(&r,borrow);assert(borrow->refcnt==1&&freed==native_count-1);r.settled_submit=17;pgraph_vk_shader_objects_retire(&r);assert(!r.shader_objects.gpu_owners&&freed==native_count);
  }free(pool);
 }
 fail_at=-1;
 for(malformed_layout=1;malformed_layout<=5;malformed_layout++){
  PGRAPHVkState r=fresh();ShaderModuleCacheEntry *pool=calloc(120,sizeof(*pool));assert(pool);r.ready_vertex.stages=pool;made=freed=returned=source_count=source_live=0;
  vk_ready_vertex_init(&r);assert(!r.ready_vertex.ready&&!r.ready_vertex.stages&&made==1&&freed==1&&returned==120&&!source_live);free(pool);
 }
 malformed_layout=0;
}
static void payload_and_inputs(void){
 PGRAPHVkState r=fresh();ShaderModuleCacheEntry *pool=calloc(120,sizeof(*pool));r.ready_vertex.stages=pool;source_count=0;vk_ready_vertex_init(&r);assert(r.ready_vertex.ready);r.ready_vertex.route=true;
 ShaderBinding binding={0};r.shader_binding=&binding;PGRAPHState pg={.vk_renderer_state=&r};binding.vsh.module_info=pool[0].module_info;binding.state.vsh.programmable.program_length=1;
 ShaderModuleCacheKey key={.kind=VK_SHADER_STAGE_VERTEX_BIT};
 for(unsigned lane=0;lane<8;lane++){key.vsh.state.programmable.program_length=1;key.vsh.state.smooth_shading=!!(lane&2);key.vsh.state.point_params_enable=!!(lane&4);key.vsh.glsl_opts.prefix_outputs=!!(lane&1);assert(vk_ready_vertex_find(&r,&key)==pool[lane].module_info);}
 for(unsigned family=0;family<112;family++){key.vsh.state=pool[8+family].key.vsh.state;key.vsh.state.uniform_attrs=65535;key.vsh.glsl_opts.prefix_outputs=!!(family&1);assert(vk_ready_vertex_find(&r,&key)==pool[8+family].module_info);}
 memset(&key,0,sizeof(key));key.kind=VK_SHADER_STAGE_VERTEX_BIT;key.vsh.state.programmable.program_length=1;assert(vk_ready_vertex_find(&r,&key)==pool[0].module_info);key.vsh.state.programmable.program_length=0;assert(!vk_ready_vertex_find(&r,&key));key.vsh.state.programmable.program_length=1;key.vsh.state.swizzle_attrs=1;assert(!vk_ready_vertex_find(&r,&key));key.vsh.state.swizzle_attrs=0;
 for(unsigned i=0;i<16;i++)for(unsigned lane=0;lane<4;lane++){uint32_t bits=0x3f000000+i*16+lane;memcpy(&pg.vertex_attributes[i].inline_value[lane],&bits,4);}
 pg.uniform_attrs=1;assert(vk_ready_vertex_route(&pg));uint32_t bad=0x7fc12345;memcpy(&pg.vertex_attributes[0].inline_value[0],&bad,4);assert(!vk_ready_vertex_route(&pg));bad=0x7f800000;memcpy(&pg.vertex_attributes[0].inline_value[0],&bad,4);assert(!vk_ready_vertex_route(&pg));bad=0x80000000;memcpy(&pg.vertex_attributes[0].inline_value[0],&bad,4);assert(vk_ready_vertex_route(&pg));bad=1;memcpy(&pg.vertex_attributes[0].inline_value[0],&bad,4);assert(vk_ready_vertex_route(&pg));
 binding.state.vsh.uniform_attrs=1;assert(vk_ready_vertex_parameters(&pg,&binding));assert(!vk_ready_vertex_parameters(&pg,&binding));ShaderModuleInfo *m=binding.vsh.module_info;
 if(TEST_PREFIX_ALIASES){binding.vsh.module_info=pool[1].module_info;assert(binding.vsh.module_info==m&&m->ready_vertex_index==1);assert(!vk_ready_vertex_parameters(&pg,&binding));binding.vsh.module_info=pool[0].module_info;}
 VkDeviceSize size=pgraph_vk_ready_vertex_upload_extent(&r,m);assert(size==r.ready_vertex.payload[0].suffix_offset+16);uint8_t *a=malloc(size+32),*b=malloc(size+32);memset(a,0xa5,size+32);pgraph_vk_ready_vertex_upload(&pg,m,a);assert(!memcmp(a,m->uniforms.allocation,m->uniforms.total_size));assert(!memcmp(a+r.ready_vertex.payload[0].suffix_offset,pg.vertex_attributes[0].inline_value,16));for(unsigned i=size;i<size+32;i++)assert(a[i]==0xa5);
 uint32_t changed=0x3fc00000;memcpy(&pg.vertex_attributes[0].inline_value[2],&changed,4);if(TEST_PREFIX_ALIASES)binding.vsh.module_info=pool[1].module_info;m->uniform_staged_valid=true;assert(vk_ready_vertex_parameters(&pg,&binding)&&!m->uniform_staged_valid);pgraph_vk_ready_vertex_upload(&pg,m,b);assert(memcmp(a,b,size));memcpy(pg.vertex_attributes[0].inline_value,a+r.ready_vertex.payload[0].suffix_offset,16);if(TEST_PREFIX_ALIASES)binding.vsh.module_info=pool[0].module_info;assert(vk_ready_vertex_parameters(&pg,&binding));pgraph_vk_ready_vertex_upload(&pg,m,b);assert(!memcmp(a,b,size));
 /* Identical raw constants with a different semantic mask still invalidate. */
 memcpy(pg.vertex_attributes[1].inline_value,pg.vertex_attributes[0].inline_value,16);binding.state.vsh.uniform_attrs=2;assert(vk_ready_vertex_parameters(&pg,&binding));
 binding.state.vsh.programmable.program_data[135][3]=0x12345678;assert(vk_ready_vertex_parameters(&pg,&binding));uint32_t got;memcpy(&got,(char*)m->uniforms.allocation+16+135*16+12,4);assert(got==0x12345678);
 for(unsigned i=0;i<36;i++){
  unsigned mask=i==0?0:i==1?65535:i==2?0xaaaa:i==3?0x5555:i<20?1u<<(i-4):65535u^(1u<<(i-20));binding.state.vsh.uniform_attrs=mask;vk_ready_vertex_parameters(&pg,&binding);
  r.num_active_vertex_binding_descriptions=r.num_active_vertex_attribute_descriptions=0;
  for(unsigned attr=0;attr<16;attr++)if(!(mask&(1u<<attr))){unsigned n=r.num_active_vertex_binding_descriptions++;r.vertex_binding_descriptions[n]=(VkVertexInputBindingDescription){.binding=n,.stride=16,.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};r.vertex_attribute_descriptions[r.num_active_vertex_attribute_descriptions++]=(VkVertexInputAttributeDescription){.location=attr,.binding=n,.format=VK_FORMAT_R32G32B32A32_SFLOAT};}
  r.vi_last.valid=false;r.storage_buffers[0].buffer=(VkBuffer)(uintptr_t)1234;r.uniform_buffer_offsets[0]=4096+i*512;input_calls=0;pgraph_vk_ready_vertex_input(&pg);assert(input_calls==1);pgraph_vk_ready_vertex_input(&pg);assert(input_calls==1);
  if(mask){assert(bound_slot==(unsigned)r.num_active_vertex_binding_descriptions);assert(bound_offset==r.uniform_buffer_offsets[0]+r.ready_vertex.payload[0].suffix_offset);assert(captured_bindings[bound_slot].stride==0);unsigned rank=0;for(unsigned attr=0;attr<16;attr++)if(mask&(1u<<attr)){bool found=false;for(unsigned at=0;at<16;at++)if(captured_attrs[at].location==attr){assert(captured_attrs[at].binding==bound_slot&&captured_attrs[at].offset==rank*16);found=true;}assert(found);rank++;}}
 }
 /* Full stage+suffix extent and upload epoch, then independent aligned reservations. */
 binding.state.vsh.uniform_attrs=1;vk_ready_vertex_parameters(&pg,&binding);size=pgraph_vk_ready_vertex_upload_extent(&r,m);
 m->uniform_staged_valid=true;m->uniform_staged_offset=512;m->uniform_staged_epoch=7;r.uniform_upload_epoch=7;r.storage_buffers[1].buffer_offset=512+size;
 assert(uniform_snapshot_valid(&r,m));r.storage_buffers[1].buffer_offset--;assert(!uniform_snapshot_valid(&r,m));r.storage_buffers[1].buffer_offset++;r.uniform_upload_epoch++;assert(!uniform_snapshot_valid(&r,m));
 r.storage_buffers[1].buffer_size=8192;r.storage_buffers[1].mapped=calloc(1,8192);r.storage_buffers[1].buffer_offset=3;void *destination;
 VkDeviceSize first=pgraph_vk_reserve_buffer(&pg,1,size,256,&destination);assert(first==256&&destination==r.storage_buffers[1].mapped+256);pgraph_vk_ready_vertex_upload(&pg,m,destination);
 VkDeviceSize second=pgraph_vk_reserve_buffer(&pg,1,31,256,&destination);assert(second==ROUND_UP(first+size,256)&&r.storage_buffers[1].buffer_offset==second+31);free(r.storage_buffers[1].mapped);
 binding.vsh.module_info=pool[8].module_info;binding.state.vsh=pool[8].key.vsh.state;binding.state.vsh.uniform_attrs=3;assert(vk_ready_vertex_parameters(&pg,&binding));
 size=pgraph_vk_ready_vertex_upload_extent(&r,binding.vsh.module_info);assert(size==288);uint8_t padded[320];memset(padded,0xa5,sizeof(padded));pgraph_vk_ready_vertex_upload(&pg,binding.vsh.module_info,padded);assert(padded[255]==0&&padded[288]==0xa5);
 free(a);free(b);vk_ready_vertex_finalize(&r);free(pool);
}
int main(void){PGRAPHVkState r=fresh();assert(pgraph_vk_prepared_vertex_supported(&r));option=false;assert(!pgraph_vk_prepared_vertex_supported(&r));option=true;r.shader_objects.enabled=false;assert(!pgraph_vk_prepared_vertex_supported(&r));r.shader_objects.enabled=true;stages();payload_and_inputs();puts("Vulkan prepared vertex:120 logical targets, all actual native partial failures, shared alias/binding/GPU last-consumer ownership; explicit ShaderEXT namespace, finite-domain changes, raw A/B/A/mask/code bytes, complete suffix extent,36mixed input masks and late final offsets PASS");return 0;}

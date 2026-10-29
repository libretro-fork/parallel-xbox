/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ROUND_UP(n,a) (((n)+(a)-1)&~((a)-1))
#define PGRAPH_VK_SLOTS 2
#define BUFFER_UNIFORM_STAGING 0
#define VK_FINISH_REASON_NEED_BUFFER_SPACE 1
typedef uint64_t VkDeviceSize;
typedef struct {size_t total_size;void *allocation;} ShaderUniformLayout;
typedef struct ShaderModuleInfo {ShaderUniformLayout uniforms;} ShaderModuleInfo;
typedef struct {struct{ShaderModuleInfo *module_info;int uniform_locs[1];}vsh,psh;struct{int vsh;}state;} ShaderBinding;
typedef struct {size_t buffer_offset,buffer_size;} StorageBuffer;
typedef struct {StorageBuffer storage_buffers[1];unsigned uniforms_changed;bool shader_bindings_changed,texture_bindings_changed;int descriptor_sets[32],descriptor_set_index,descriptor_set_base;ShaderBinding *shader_binding;struct{struct{size_t minUniformBufferOffsetAlignment;}limits;}device_props;size_t uniform_buffer_offsets[2];const ShaderModuleInfo *uni_last_pushed[2];struct{int values;}vsh_uni_src;uint8_t gpu[512];unsigned copies[2],finishes;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef int PshUniformValues;
static const int VshUniformInfo[1]={0},PshUniformInfo[1]={0};
static const int8_t pgraph_vsh_uniform_section[1]={0};
enum {VshUniform__COUNT=1,PshUniform__COUNT=1};
static bool pending[2];
static void pgraph_vsh_uniform_source_refresh(PGRAPHState*p,void*s,void*v,void*l){}
static void pgraph_glsl_set_psh_uniform_values(PGRAPHState*p,int*l,PshUniformValues*v){*v=0;}
static void set_psh_texscale_values(PGRAPHState*p,PshUniformValues*v){}
static bool vk_apply_uniform_updates(PGRAPHVkState*r,ShaderUniformLayout*l,const int*i,int*loc,void*values,size_t n,const ShaderModuleInfo*m,const int8_t*sections)
{return pending[m==r->shader_binding->psh.module_info];}
static bool pgraph_vk_buffer_has_space_for(PGRAPHState*p,int index,VkDeviceSize n,size_t align)
{StorageBuffer*b=&p->vk_renderer_state->storage_buffers[index];return ROUND_UP(b->buffer_offset,align)+n<=b->buffer_size;}
static void pgraph_vk_finish(PGRAPHState*p,int reason)
{PGRAPHVkState*r=p->vk_renderer_state;r->finishes++;r->storage_buffers[0].buffer_offset=0;r->descriptor_set_index=r->descriptor_set_base=0;memset(r->gpu,0xcd,sizeof(r->gpu));}
static size_t pgraph_vk_append_to_buffer(PGRAPHState*p,int index,void**data,VkDeviceSize*sizes,size_t n,size_t align)
{assert(n==1);PGRAPHVkState*r=p->vk_renderer_state;StorageBuffer*b=&r->storage_buffers[index];size_t at=ROUND_UP(b->buffer_offset,align);assert(at+sizes[0]<=b->buffer_size);memcpy(r->gpu+at,data[0],sizes[0]);b->buffer_offset=at+sizes[0];r->copies[data[0]==r->shader_binding->psh.module_info->uniforms.allocation]++;return at;}
/* PRODUCTION */
static uint32_t seed=17;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void)
{
 uint8_t blocks[4][51];ShaderModuleInfo mods[4];for(unsigned i=0;i<4;i++){memset(blocks[i],i+1,sizeof(blocks[i]));mods[i].uniforms=(ShaderUniformLayout){i%2?51:19,blocks[i]};}
 ShaderBinding b={.vsh.module_info=&mods[0],.psh.module_info=&mods[1]};
 PGRAPHVkState r={.shader_binding=&b,.device_props.limits.minUniformBufferOffsetAlignment=16,.storage_buffers={{0,512}}};PGRAPHState pg={&r};
 pending[0]=pending[1]=false;update_shader_uniforms_tracked(&pg);assert(r.uniforms_changed==3);pgraph_vk_update_descriptor_sets(&pg);assert(r.copies[0]==1&&r.copies[1]==1);
 /* Mutating only one stage must not duplicate its unchanged partner. */
 for(unsigned stage=0;stage<2;stage++){
  pending[0]=pending[1]=false;pending[stage]=true;blocks[stage][0]++;
  unsigned old=r.copies[1-stage];size_t offset=r.uniform_buffer_offsets[1-stage];
  update_shader_uniforms_tracked(&pg);assert(r.uniforms_changed==(1u<<stage));pgraph_vk_update_descriptor_sets(&pg);
  assert(r.copies[1-stage]==old&&r.uniform_buffer_offsets[1-stage]==offset);
 }
 /* Repeatedly switch modules, dirty either/both stages, exhaust descriptor
  * slots and upload space, and force external buffer resets. */
 for(unsigned trial=0;trial<100000;trial++){
  uint32_t choice=rnd();b.vsh.module_info=&mods[(choice&1)?2:0];b.psh.module_info=&mods[(choice&2)?3:1];
  pending[0]=choice&4;pending[1]=choice&8;
  for(unsigned stage=0;stage<2;stage++)if(pending[stage]){ShaderModuleInfo*m=stage?b.psh.module_info:b.vsh.module_info;((uint8_t*)m->uniforms.allocation)[trial%m->uniforms.total_size]++;}
  r.texture_bindings_changed=choice&16;r.shader_bindings_changed=choice&32;
  if((choice&255)==0){r.storage_buffers[0].buffer_offset=0;memset(r.gpu,0xef,sizeof(r.gpu));}
  update_shader_uniforms_tracked(&pg);pgraph_vk_update_descriptor_sets(&pg);
  assert(!r.uniforms_changed);
  ShaderUniformLayout*l[2]={&b.vsh.module_info->uniforms,&b.psh.module_info->uniforms};
  for(unsigned i=0;i<2;i++){assert(r.uniform_buffer_offsets[i]%16==0);assert(memcmp(r.gpu+r.uniform_buffer_offsets[i],l[i]->allocation,l[i]->total_size)==0);}
 }
 printf("PASS: independent uniform stages, module switches, aligned capacity, external resets and descriptor exhaustion across 100000 mixed draws (%u resets)\n",r.finishes);
}

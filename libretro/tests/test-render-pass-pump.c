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
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define FALSE false
#define g_new(t,n) ((t *)malloc(sizeof(t)*(n)))
#define g_free free
typedef struct GArray { unsigned len,cap;size_t size;void *data; } GArray;
#define g_array_index(a,t,i) (((t *)(a)->data)[i])
static GArray *g_array_new(bool a,bool b,size_t size){GArray *p=calloc(1,sizeof(*p));assert(p);p->size=size;return p;}
static void append(GArray *a,const void *p){if(a->len==a->cap){a->cap=a->cap?2*a->cap:8;a->data=realloc(a->data,a->size*a->cap);assert(a->data);}memcpy((char *)a->data+a->size*a->len++,p,a->size);}
#define g_array_append_val(a,v) append(a,&(v))
static void g_array_remove_index_fast(GArray *a,unsigned i){assert(i<a->len);a->len--;if(i!=a->len)memcpy((char *)a->data+i*a->size,(char *)a->data+a->len*a->size,a->size);}
static void array_free(GArray *a){if(a){free(a->data);free(a);}}
/* TYPES */
typedef struct PGRAPHVkState {
 bool device,dyn_cull_front,dyn_blend,dyn_vertex_input,color_binding,zeta_binding;
 GArray *render_passes;RenderPassState live;
 PumpProgress pump;
} PGRAPHVkState;
struct PGRAPHState {PGRAPHVkState *vk_renderer_state;};
static struct{struct{bool shader_seeding;}perf;}g_config;
static bool pipe_started,pressure;
static unsigned pump_d,pump_w_local,drains,probes,submits,bank_n;
static ShaderState states[40];static uint64_t hashes[40];static bool ready[40];
static unsigned observed[6][40];
static uint64_t xxh3_64(const uint8_t *p,size_t n, uint64_t seed __attribute__((unused))){uint64_t h=1469598103934665603ull;while(n--)h=(h^*p++)*1099511628211ull;return h;}
static void pump_drain(PGRAPHState *pg){drains++;pump_d=pump_w_local;}
static const ShaderState *pgraph_vk_seed_pump_states(unsigned *n){*n=bank_n;return states;}
static bool pgraph_vk_seed_spv_ready_hash(uint64_t h){probes++;for(unsigned i=0;i<bank_n;i++)if(h==hashes[i])return ready[i];abort();}
static int pump_state(PGRAPHState *pg,const ShaderState *st,const RenderPassState *rp){
 assert(st>=states&&st<states+bank_n);assert(++submits<=4);
 if(pressure)return -1;
 for(unsigned i=0;i<pg->vk_renderer_state->render_passes->len;i++)if(!memcmp(rp,&g_array_index(pg->vk_renderer_state->render_passes,RenderPass,i).state,sizeof(*rp))){observed[i][st-states]++;return 1;}
 abort();
}
/* PRODUCTION */
static void attachments(PGRAPHVkState *r,const RenderPassState *rp){
 struct{RenderPassState render_pass_state;}owned={*rp};const typeof(owned)*key=&owned;
 struct{VkPipelineDepthStencilStateCreateInfo ds;}value={0},*job=&value;
 VkPipelineDepthStencilStateCreateInfo depth_stencil={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
 /* DEPTH */
 unsigned count=/* COLOR */;
 assert(count==(rp->color_format!=VK_FORMAT_UNDEFINED));
 assert(!!job->ds.sType==(rp->zeta_format!=VK_FORMAT_UNDEFINED));
}
int main(void){
 _Static_assert(sizeof(void *)!=8||sizeof(PumpProgress)==sizeof(PumpProgressBefore),"cursor must fit existing 64-bit progress padding");
 unsigned cases=0;
 for(unsigned count=0;count<=6;count++)for(unsigned mode=0;mode<16;mode++){
  PGRAPHVkState r={.device=true,.dyn_cull_front=true,.dyn_blend=true,.dyn_vertex_input=true,.color_binding=true};PGRAPHState pg={&r};
  r.render_passes=g_array_new(false,false,sizeof(RenderPass));r.live=(RenderPassState){VK_FORMAT_R32_SFLOAT,VK_FORMAT_D32_SFLOAT};
  for(unsigned i=0;i<count;i++){RenderPass pass={.state={i&1?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_B8G8R8A8_UNORM,i&2?VK_FORMAT_D16_UNORM:VK_FORMAT_UNDEFINED}};if(i>=4)pass.state.color_format=VK_FORMAT_R16G16B16A16_SFLOAT+i;g_array_append_val(r.render_passes,pass);}
  bank_n=40;for(unsigned i=0;i<bank_n;i++){memset(&states[i],0,sizeof(states[i]));states[i].psh.final_inputs_0=i+1;hashes[i]=xxh3_64((const uint8_t *)&states[i],sizeof(states[i]), 0);ready[i]=!(mode&2);}
  memset(observed,0,sizeof(observed));g_config.perf.shader_seeding=!(mode&1);pipe_started=true;pump_d=0;pump_w_local=1;drains=0;pressure=mode&4;
  bank_n=0;probes=submits=0;pgraph_vk_libpump_step(&pg);
  assert(!probes&&!submits&&!r.pump.n&&!r.pump.next_pass&&!r.pump.state_hash);
  bank_n=40;cases++;
  for(unsigned flip=0;flip<160;flip++){
   if(flip==16){for(unsigned i=0;i<bank_n;i++)ready[i]=true;pressure=false;}
   if((mode&8)&&count&&flip==20){
    /* The native owner relocates, preserving its format identities. */
    void *old=r.render_passes->data;r.render_passes->data=malloc(r.render_passes->size*r.render_passes->cap);assert(r.render_passes->data);memcpy(r.render_passes->data,old,r.render_passes->size*r.render_passes->len);memset(old,0xcd,r.render_passes->size*r.render_passes->len);free(old);
   }
   probes=submits=0;pgraph_vk_libpump_step(&pg);assert(probes<=32&&submits<=4&&r.pump.n<=4);
   if(!g_config.perf.shader_seeding||!count)assert(submits==0&&r.pump.n==0&&r.pump.next_pass==0);
   cases++;
  }
  assert(drains==1);
  for(unsigned i=0;i<count&&i<4;i++)for(unsigned j=0;j<bank_n;j++)assert(observed[i][j]==(unsigned)g_config.perf.shader_seeding);
  /* A shortened native owner must normalize an old cursor before borrowing. */
  if(count){r.pump.next_pass=999;RenderPassState out;assert(pump_next_render_pass(&r,&out));assert(!memcmp(&out,&g_array_index(r.render_passes,RenderPass,0).state,sizeof(out)));}
  for(unsigned color=0;color<2;color++)for(unsigned depth=0;depth<2;depth++)for(unsigned live=0;live<4;live++){
   r.color_binding=live&1;r.zeta_binding=live&2;RenderPassState rp={color?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_UNDEFINED,depth?VK_FORMAT_D16_UNORM:VK_FORMAT_UNDEFINED};attachments(&r,&rp);cases++;
  }
  for(unsigned i=0;i<r.pump.n;i++){array_free(r.pump.retry[i]);}
  free(r.pump.state_hash);array_free(r.render_passes);
 }
 printf("Render-pass pump: %u bounded cold/ready/delayed/pressure, relocated-owner, capacity, fairness and key-owned attachment cases PASS; one format snapshot=%zu bytes, progress owner=%zu -> %zu bytes\n",cases,sizeof(RenderPassState),sizeof(PumpProgressBefore),sizeof(PumpProgress));
}

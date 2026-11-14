/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "hw/xbox/nv2a/pgraph/blend.h"
#include <encodings/xxh3.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL seed-full line %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define ARRAY_SIZE(a) ((int)(sizeof(a)/sizeof(*(a))))
#define QEMU_BUILD_BUG_ON(x) _Static_assert(!(x),"key layout")
/* TYPES */
typedef struct { ShaderState state; } ShaderBinding;
typedef struct { unsigned refs; } ShaderModuleInfo;
typedef struct {PipelineKey key;uint64_t hash,state_hash;bool full;ShaderModuleInfo *mods[3];unsigned nmods;} LibJob;
typedef struct {
    bool dyn_cull_front,dyn_blend,dyn_vertex_input;
    ShaderBinding *shader_binding;RenderPassState rp;uint32_t raw[9];
    unsigned num_active_vertex_binding_descriptions,num_active_vertex_attribute_descriptions;
    VkVertexInputBindingDescription vertex_binding_descriptions[16];
    VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
} PGRAPHVkState;
struct PGRAPHState {PGRAPHVkState *vk_renderer_state;struct{unsigned color_format;}surface_shape;};
static LibJob slot;
static PipelineKey expected;
static ShaderModuleInfo mods[3];
static bool full,ready,flight,geometry;
static unsigned refs,submits,queued;
uint64_t cpu_features_get(void){return 0;}
/* REGISTERS */
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned reg){for(unsigned i=0;i<9;i++)if(pipeline_registers[i]==reg)return pg->vk_renderer_state->raw[i];abort();}
static void init_render_pass_state(PGRAPHState *pg,RenderPassState *rp){*rp=pg->vk_renderer_state->rp;}
static LibJob *pump_slot(void){if(full)return NULL;memset(&slot,0xa5,sizeof(slot));slot.nmods=0;return &slot;}
static bool pipeline_ready(PGRAPHVkState *r,uint64_t hash,const PipelineKey *key){(void)r;assert(key==&slot.key&&!memcmp(key,&expected,sizeof(*key)));assert(hash==xxh3_64((const uint8_t *)&expected,sizeof(expected), 0));return ready;}
static bool pump_full_in_flight(uint64_t hash,const PipelineKey *key){assert(!memcmp(key,&expected,sizeof(*key)));assert(hash==xxh3_64((const uint8_t *)&expected,sizeof(expected), 0));return flight;}
static void pgraph_vk_ref_state_modules(PGRAPHVkState *r,const ShaderState *st,ShaderModuleInfo **g,ShaderModuleInfo **v,ShaderModuleInfo **p){(void)r;assert(st==&slot.key.shader_state);refs++;*v=&mods[0];*g=geometry?&mods[1]:NULL;*p=&mods[2];}
static void pipeline_job_fill(PGRAPHState *pg,const ShaderState *st,const ShaderModuleInfo *v,const ShaderModuleInfo *g,const ShaderModuleInfo *p,const PipelineKey *key,LibJob *job){(void)pg;assert(st==&slot.key.shader_state&&job==&slot&&key==&slot.key);assert(v==&mods[0]&&g==(geometry?&mods[1]:NULL)&&p==&mods[2]);slot.full=true;}
static void pump_submit(LibJob *job){assert(job==&slot&&slot.full);assert(job->nmods==(geometry?3u:2u));assert(job->mods[0]==&mods[0]&&job->mods[job->nmods-1]==&mods[2]);assert(job->state_hash==xxh3_64((const uint8_t *)&expected.shader_state,sizeof(expected.shader_state), 0));submits++;}
static void pgraph_vk_spec_pipeline_queued(uint64_t hash){assert(submits==1&&hash==slot.state_hash);queued++;}
/* FUNCTIONS */
static uint32_t rng=0x48276ad1;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
    unsigned cases=0;
    for(unsigned n=0;n<1024;n++)for(unsigned bits=0;bits<16;bits++){
        ShaderBinding binding;for(size_t i=0;i<sizeof(binding.state);i++)((uint8_t *)&binding.state)[i]=(uint8_t)random32();
        PGRAPHVkState r={.dyn_cull_front=true,.dyn_blend=true,.dyn_vertex_input=true,.shader_binding=&binding,.rp={(VkFormat)(n%7),(VkFormat)(n%3)}};
        for(unsigned i=0;i<9;i++)r.raw[i]=random32();
        memset(r.vertex_binding_descriptions,0x5a,sizeof(r.vertex_binding_descriptions));
        memset(r.vertex_attribute_descriptions,0xa5,sizeof(r.vertex_attribute_descriptions));
        r.num_active_vertex_binding_descriptions=r.num_active_vertex_attribute_descriptions=16;
        PGRAPHState pg={.vk_renderer_state=&r};
        init_pipeline_key(&pg,&expected);
        ShaderState seed=binding.state;RenderPassState rp=r.rp;
        /* Seed preparation cannot depend on any currently bound shader. */
        r.shader_binding=NULL;full=bits&1;ready=bits&2;flight=bits&4;geometry=bits&8;
        refs=submits=queued=0;
        int result=pump_state(&pg,&seed,&rp);
        int want=full?-1:(ready||flight)?0:1;
        assert(result==want&&refs==(unsigned)(want==1)&&submits==(unsigned)(want==1)&&queued==submits);
        if(want==1){
            memset(&seed,0xcc,sizeof(seed));memset(&rp,0x44,sizeof(rp));
            assert(!memcmp(&slot.key,&expected,sizeof(expected)));
        }
        cases++;
    }
    printf("Seeded full PSOs: %u actual-key/random-register/slot/backpressure/ready/in-flight/module-owner/seed-reuse cases PASS\n",cases);
    return 0;
}

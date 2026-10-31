/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef TEST_VULKAN
#include <volk.h>
#else
#include <vulkan/vulkan.h>
#endif
#include "hw/xbox/nv2a/pgraph/spv/depth.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "hw/xbox/nv2a/pgraph/vk/compute-dispatch.h"
#include "deps/SPIRV-Reflect/spirv_reflect.h"
#include "depth-reference-glsl.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
static unsigned allocations;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void*,size_t);
void *__wrap_malloc(size_t n){allocations++;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t m){allocations++;return __real_calloc(n,m);}
void *__wrap_realloc(void*p,size_t n){allocations++;return __real_realloc(p,n);}
typedef struct {VkDevice device;VkPipelineCache vk_pipeline_cache;VkPhysicalDeviceProperties device_props;struct{VkPipelineLayout pipeline_layout;VkPipeline pipelines[2][2];uint32_t workgroup_size;}compute;} PGRAPHVkState;
static unsigned created_modules,removed_modules,created_pipelines,removed_pipelines,expected_group;
static VkResult spy_module(VkDevice d,const VkShaderModuleCreateInfo*i,const VkAllocationCallbacks*a,VkShaderModule*m){(void)d;(void)a;SpvStaticModule s=pgraph_spv_depth_compute(created_modules/2,created_modules%2,expected_group);assert(created_modules<4&&s.words==i->pCode&&s.num_words*4==i->codeSize);*m=(VkShaderModule)(uintptr_t)(++created_modules);return VK_SUCCESS;}
static void spy_drop_module(VkDevice d,VkShaderModule m,const VkAllocationCallbacks*a){(void)d;(void)a;assert((uintptr_t)m==++removed_modules);}
static VkResult spy_pipeline(VkDevice d,VkPipelineCache c,uint32_t n,const VkComputePipelineCreateInfo*i,const VkAllocationCallbacks*a,VkPipeline*p){(void)d;(void)c;(void)a;assert(n==1&&i->stage.stage==VK_SHADER_STAGE_COMPUTE_BIT&&!i->stage.pSpecializationInfo);assert((uintptr_t)i->stage.module==created_pipelines+1&&!strcmp(i->stage.pName,"main"));*p=(VkPipeline)(uintptr_t)(++created_pipelines);return VK_SUCCESS;}
static void spy_drop_pipeline(VkDevice d,VkPipeline p,const VkAllocationCallbacks*a){(void)d;(void)a;assert((uintptr_t)p==++removed_pipelines);}
#define vkCreateShaderModule spy_module
#define vkDestroyShaderModule spy_drop_module
#define vkCreateComputePipelines spy_pipeline
#define vkDestroyPipeline spy_drop_pipeline
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
/* PRODUCTION */
#undef vkCreateShaderModule
#undef vkDestroyShaderModule
#undef vkCreateComputePipelines
#undef vkDestroyPipeline
/* QUIET ERRORS */
static void save(const char*folder,const char*name,const uint32_t*w,size_t n){char path[2048];snprintf(path,sizeof(path),"%s/%s.spv",folder,name);FILE*f=fopen(path,"wb");assert(f&&fwrite(w,4,n,f)==n);assert(!fclose(f));}
static GlslcResult compile(const char*source){GlslcOptions o={.spirv_1_6=true};GlslcResult r;if(!glslc_compile(GLSLC_STAGE_COMPUTE,source,&o,&r)){fprintf(stderr,"Depth reference line %d: %s\n",r.error_line,r.error);abort();}return r;}
static GlslcResult reference(bool f,bool pack,unsigned group){const char*body=f?(pack?pack_d32_sfloat_s8_uint_to_z24s8_glsl:unpack_z24s8_to_d32_sfloat_s8_uint_glsl):(pack?pack_d24_unorm_s8_uint_to_z24s8_glsl:unpack_z24s8_to_d24_unorm_s8_uint_glsl);char source[8192];snprintf(source,sizeof(source),"#version 450\nlayout(local_size_x=%u) in;\n%s",group,body);return compile(source);}
static void grid_tests(void){
 ComputeGrid g;assert(!pgraph_compute_grid(1,0,1,1,&g));assert(!pgraph_compute_grid(1,128,0,1,&g));assert(!pgraph_compute_grid(1000,128,1,1,&g));assert(!pgraph_compute_grid(UINT32_MAX,256,65535,65535,&g));
 uint32_t seed=1;for(unsigned i=0;i<100000;i++){seed=seed*1664525u+1013904223u;uint32_t pixels=seed&0xffffff,group=i&1?128:256,maxx=(seed>>24)+1,maxy=65535;bool valid=pgraph_compute_grid(pixels,group,maxx,maxy,&g);if(!valid){assert(((uint64_t)pixels+group-1)/group>(uint64_t)maxx*maxy);continue;}assert(g.x<=maxx&&g.y<=maxy);uint64_t covered=(uint64_t)g.x*g.y*group;assert(covered>=pixels&&covered-pixels<(uint64_t)g.y*group);}
 for(unsigned n=0;n<1024;n++){assert(pgraph_compute_grid(n,128,3,32,&g));unsigned char seen[1024]={0};for(unsigned y=0;y<g.y;y++)for(unsigned x=0;x<g.x;x++)for(unsigned local=0;local<128;local++){unsigned at=y*g.x*128+x*128+local;if(at<n)seen[at]++;}for(unsigned at=0;at<n;at++)assert(seen[at]==1);}
 assert(pgraph_compute_grid(5760u*4320u,256,65535,65535,&g)&&g.y>1);assert((uint64_t)g.x*g.y*256==5760u*4320u);
 assert(pgraph_compute_grid(8192u*8192u,128,65535,65535,&g)&&g.y>1);
 puts("Compute grid: 100000 bounded grids and 1024 exhaustive coverage cases passed");
}
static void host_tests(void){
 for(unsigned limit=128;limit<=1024;limit*=2){PGRAPHVkState r={0};r.device_props.limits.maxComputeWorkGroupSize[0]=limit;r.device_props.limits.maxComputeWorkGroupInvocations=limit;r.device_props.limits.maxComputeWorkGroupCount[0]=65535;r.device_props.limits.maxComputeWorkGroupCount[1]=65535;created_modules=removed_modules=created_pipelines=removed_pipelines=0;expected_group=limit>=256?256:128;allocations=0;create_compute_pipelines(&r);assert(!allocations&&created_modules==4&&removed_modules==4&&created_pipelines==4);for(unsigned i=0;i<100000;i++){VkPipeline p=get_compute_pipeline(&r,i&1?VK_FORMAT_D32_SFLOAT_S8_UINT:VK_FORMAT_D24_UNORM_S8_UINT,(i>>1)&1);assert(p);ComputeGrid g=get_dispatch(&r,i);assert((uint64_t)g.x*g.y*r.compute.workgroup_size>=i);}assert(!allocations&&created_modules==4&&created_pipelines==4);destroy_compute_pipelines(&r);assert(removed_pipelines==4&&!allocations);for(unsigned f=0;f<2;f++)for(unsigned p=0;p<2;p++)assert(!r.compute.pipelines[f][p]);}
 puts("Depth pipelines: four prepared handles, zero core allocations and 400000 lookups without driver calls passed");
}
#ifdef TEST_VULKAN
#include "gpu-common.h"
#include "spv-depth-vulkan.h"
#endif
int main(int argc,char**argv){quiet_test_errors();setvbuf(stdout,NULL,_IONBF,0);assert(argc==2);grid_tests();host_tests();for(unsigned group=128;group<=256;group*=2)for(unsigned f=0;f<2;f++)for(unsigned pack=0;pack<2;pack++){SpvStaticModule s=pgraph_spv_depth_compute(f,pack,group);allocations=0;for(unsigned i=0;i<1000;i++){SpvStaticModule t=pgraph_spv_depth_compute(f,pack,group);assert(t.words==s.words&&t.num_words==s.num_words);}assert(!allocations);char name[64];snprintf(name,sizeof(name),"static-%u-%u-%u",group,f,pack);save(argv[1],name,s.words,s.num_words);SpvReflectShaderModule m;assert(spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NO_COPY,s.num_words*4,s.words,&m)==SPV_REFLECT_RESULT_SUCCESS);assert(m.descriptor_binding_count==3&&m.push_constant_block_count==1&&m.push_constant_blocks[0].member_count==4&&m.push_constant_blocks[0].size==16);for(unsigned b=0;b<3;b++)assert(m.descriptor_bindings[b].binding==b&&m.descriptor_bindings[b].descriptor_type==SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER);spvReflectDestroyShaderModule(&m);GlslcResult ref=reference(f,pack,group);snprintf(name,sizeof(name),"reference-%u-%u-%u",group,f,pack);save(argv[1],name,ref.words,ref.num_words);free(ref.words);}
 assert(!pgraph_spv_depth_compute(false,false,64).words);
#ifdef TEST_VULKAN
 depth_gpu_tests(argv[1]);
#endif
 puts("Depth SPIR-V: immutable kernels, three buffer bindings, 16-byte push layout and zero payload allocation passed");}

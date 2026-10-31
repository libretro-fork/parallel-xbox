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
#include "hw/xbox/nv2a/pgraph/spv/clear.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "deps/SPIRV-Reflect/spirv_reflect.h"
#include "clear-reference-glsl.h"
static unsigned allocations;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void*,size_t);
void *__wrap_malloc(size_t n){allocations++;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t m){allocations++;return __real_calloc(n,m);}
void *__wrap_realloc(void*p,size_t n){allocations++;return __real_realloc(p,n);}
typedef struct {VkDevice device;VkShaderModule quad_vert_module,solid_frag_module;} PGRAPHVkState;
typedef struct {PGRAPHVkState*vk_renderer_state;} PGRAPHState;
static unsigned creates,destroys;
static VkResult spy_create(VkDevice device,const VkShaderModuleCreateInfo*i,const VkAllocationCallbacks*a,VkShaderModule*m){(void)device;(void)a;assert(creates<2);SpvStaticModule span=creates?pgraph_spv_solid_fragment():pgraph_spv_fullscreen_vertex();assert(i->pCode==span.words&&i->codeSize==span.num_words*4);assert(i->sType==VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO&&!i->pNext&&!i->flags);*m=(VkShaderModule)(uintptr_t)++creates;return VK_SUCCESS;}
static void spy_destroy(VkDevice d,VkShaderModule m,const VkAllocationCallbacks*a){(void)d;(void)a;assert((uintptr_t)m==++destroys);}
#define vkCreateShaderModule spy_create
#define vkDestroyShaderModule spy_destroy
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
/* PRODUCTION */
#undef vkCreateShaderModule
#undef vkDestroyShaderModule
/* QUIET ERRORS */
static void save(const char*folder,const char*name,const uint32_t*w,size_t n){char path[2048];snprintf(path,sizeof(path),"%s/%s.spv",folder,name);FILE*f=fopen(path,"wb");assert(f&&fwrite(w,4,n,f)==n);assert(!fclose(f));}
static GlslcResult compile(GlslcStage stage,const char*s,bool version){GlslcOptions o={.spirv_1_6=version};GlslcResult r;if(!glslc_compile(stage,s,&o,&r)){fprintf(stderr,"Utility reference error line %d: %s\n",r.error_line,r.error);abort();}return r;}
static void layout_empty(const uint32_t*w,size_t n){SpvReflectShaderModule m;assert(spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NO_COPY,n*4,w,&m)==SPV_REFLECT_RESULT_SUCCESS);assert(!m.descriptor_binding_count&&!m.push_constant_block_count);assert(spvReflectGetCode(&m)==w);spvReflectDestroyShaderModule(&m);}
#ifdef TEST_VULKAN
#include "spv-clear-vulkan.h"
#endif
int main(int argc,char**argv){
 quiet_test_errors();assert(argc==2);SpvStaticModule vertex=pgraph_spv_fullscreen_vertex(),fragment=pgraph_spv_solid_fragment();
 allocations=0;for(unsigned i=0;i<100000;i++){SpvStaticModule v=pgraph_spv_fullscreen_vertex(),f=pgraph_spv_solid_fragment();assert(v.words==vertex.words&&v.num_words==vertex.num_words&&f.words==fragment.words&&f.num_words==fragment.num_words);}assert(!allocations);
 save(argv[1],"static-vertex",vertex.words,vertex.num_words);save(argv[1],"static-fragment",fragment.words,fragment.num_words);layout_empty(vertex.words,vertex.num_words);layout_empty(fragment.words,fragment.num_words);
 PGRAPHVkState state={0};PGRAPHState pg={.vk_renderer_state=&state};allocations=0;init_clear_shaders(&pg);assert(!allocations&&creates==2&&!destroys);assert(state.quad_vert_module&&state.solid_frag_module);finalize_clear_shaders(&pg);assert(!allocations&&destroys==2&&!state.quad_vert_module&&!state.solid_frag_module);
 for(unsigned version=0;version<2;version++){
  allocations=0;GlslcResult v=compile(GLSLC_STAGE_VERTEX,quad_glsl,version);unsigned va=allocations;allocations=0;GlslcResult f=compile(GLSLC_STAGE_FRAGMENT,solid_frag_glsl,version);unsigned fa=allocations;char name[64];snprintf(name,sizeof(name),"reference-vertex-%u",version);save(argv[1],name,v.words,v.num_words);snprintf(name,sizeof(name),"reference-fragment-%u",version);save(argv[1],name,f.words,f.num_words);layout_empty(v.words,v.num_words);layout_empty(f.words,f.num_words);
  printf("Utility SPV %u: vertex %zu words/%u allocations -> %zu words/zero payload allocations; fragment %zu words/%u allocations -> %zu words/zero payload allocations\n",version,v.num_words,va,vertex.num_words,f.num_words,fa,fragment.num_words);
#ifdef TEST_VULKAN
  gpu_test(argv[1],version,&v,&f,vertex,fragment);
#endif
  free(v.words);free(f.words);
 }
#ifdef TEST_VULKAN
 gpu_finish();
#endif
 puts("Utility SPIR-V: immutable words, borrowed Vulkan pointer, empty layouts and native module ownership passed");
}

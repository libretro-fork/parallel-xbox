/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/spv/utility.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "hw/xbox/nv2a/pgraph/spv/utility-glsl.h"
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
static volatile unsigned allocations;
void *__wrap_malloc(size_t n){allocations++;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t z){allocations++;return __real_calloc(n,z);}
void *__wrap_realloc(void *p,size_t n){allocations++;return __real_realloc(p,n);}
#ifndef GENERATE_UTILITY_WORDS
typedef struct { unsigned device; } PGRAPHVkState;
typedef unsigned VkShaderModule;
static bool pgraph_vk_spirv_1_6;
static SpvStaticModule expected;
static unsigned creations;
static VkShaderModule pgraph_vk_create_shader_module_from_words(PGRAPHVkState *r,const uint32_t *words,size_t count)
{
 assert(r->device==7&&words==expected.words&&count==expected.num_words);return ++creations;
}
/* PRODUCTION */
#endif
int main(int argc,char **argv)
{
 assert(argc==2);
 unsigned compiler_allocations=0;
 const char *source[]={pvideo_convert_glsl,vertex_shader_source,geometry_shader_source,fragment_shader_source,s3tc_vk_array_glsl,s3tc_vk_volume_glsl};
 const GlslcStage stage[]={GLSLC_STAGE_COMPUTE,GLSLC_STAGE_VERTEX,GLSLC_STAGE_GEOMETRY,GLSLC_STAGE_FRAGMENT,GLSLC_STAGE_COMPUTE,GLSLC_STAGE_COMPUTE};
#ifdef GENERATE_UTILITY_WORDS
 char output[4096];snprintf(output,sizeof(output),"%s/regenerate.inc",argv[1]);FILE *generated=fopen(output,"wb");assert(generated);fputs("/* SPDX-License-Identifier: GPL-2.0-or-later */\n/* Generated explicitly by test-spv-utility.py --generate; CI compares, never regenerates. */\n",generated);
#endif
 for(unsigned v=0;v<2;v++)for(unsigned s=0;s<SPV_UTILITY_COUNT;s++){
  unsigned compile_before=allocations;GlslcResult ref;GlslcOptions opts={.spirv_1_6=v,.module_prefix_words=256};assert(glslc_compile(stage[s],source[s],&opts,&ref));compiler_allocations+=allocations-compile_before;
#ifdef GENERATE_UTILITY_WORDS
  fprintf(generated,"static const uint32_t utility_%u_%u[] = {\n",v,s);
  for(size_t w=0;w<ref.num_words;w++)fprintf(generated,"0x%08xu,%s",ref.words[w],((w+1)%8==0 || w+1==ref.num_words)?"\n":" ");fputs("};\n",generated);
#else
  expected=pgraph_spv_utility(s,v);
  if(!expected.words||expected.num_words!=ref.num_words||memcmp(expected.words,ref.words,ref.num_words*4)){
   fprintf(stderr,"utility=%u target=%u words static=%zu reference=%zu\n",s,v,expected.num_words,ref.num_words);
   unsigned shown=0;for(size_t w=0;w<expected.num_words&&w<ref.num_words&&shown<16;w++)if(expected.words[w]!=ref.words[w]){fprintf(stderr,"word %zu: static=%08x reference=%08x\n",w,expected.words[w],ref.words[w]);shown++;}
   assert(!"utility reference words differ");
  }
  unsigned before=allocations;pgraph_vk_spirv_1_6=v;PGRAPHVkState r={7};
  for(unsigned i=0;i<10000;i++){SpvStaticModule m=pgraph_spv_utility(s,v);assert(m.words==expected.words&&m.num_words==expected.num_words);unsigned previous=creations;assert(pgraph_vk_create_utility_shader_module(&r,s)==previous+1);}
  assert(allocations==before);
#endif
  char path[4096];snprintf(path,sizeof(path),"%s/utility-%u-%u.spv",argv[1],v,s);FILE *f=fopen(path,"wb");assert(f&&fwrite(ref.words,4,ref.num_words,f)==ref.num_words);fclose(f);glslc_result_free_words(&ref);
 }
#ifdef GENERATE_UTILITY_WORDS
 fclose(generated);
#else
 unsigned before=allocations;assert(!pgraph_spv_utility(-1,false).words&&!pgraph_spv_utility(SPV_UTILITY_COUNT,true).num_words&&allocations==before);assert(creations==120000);
 puts("Static utilities: 12 exact reference modules; 120000 borrowed creations, exact pointer/length, zero selector/constructor allocations; invalid selectors rejected PASS");
#endif
 printf("Reference compiler heap calls: %u across 12 program/target variants; native selector/handoff remains zero\n",compiler_allocations);
 return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "hw/xbox/nv2a/pgraph/spv/geom.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
static unsigned allocations;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);
void *__wrap_malloc(size_t n){allocations++;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s){allocations++;return __real_calloc(n,s);}
void *__wrap_realloc(void*p,size_t n){allocations++;return __real_realloc(p,n);}
/* Only the string accumulator is a test shim; geometry GLSL comes from the
 * actual generator below. Timing excludes generation of this reference text. */
typedef struct {char *str;size_t len,cap;} MString;
static void appendv(MString *s,const char *fmt,va_list args){
 va_list copy;va_copy(copy,args);int n=vsnprintf(NULL,0,fmt,copy);va_end(copy);assert(n>=0);
 if(s->len+(size_t)n+1>s->cap){s->cap=(s->len+n+1)*2;s->str=realloc(s->str,s->cap);assert(s->str);}
 vsnprintf(s->str+s->len,s->cap-s->len,fmt,args);s->len+=n;
}
static MString *mstring_from_fmt(const char *fmt,...){MString*s=calloc(1,sizeof(*s));assert(s);va_list a;va_start(a,fmt);appendv(s,fmt,a);va_end(a);return s;}
static void mstring_append_fmt(MString*s,const char*fmt,...){va_list a;va_start(a,fmt);appendv(s,fmt,a);va_end(a);}
static void mstring_append(MString*s,const char*t){mstring_append_fmt(s,"%s",t);}
static void mstring_unref(MString*s){free(s->str);free(s);}
/* TYPES */
/* GENERATORS */
#ifdef TEST_VULKAN
#include <volk.h>
#else
#define VK_SHADER_STAGE_GEOMETRY_BIT 8
#endif
static bool pgraph_vk_spirv_1_6;
typedef struct {unsigned kind;struct {GeomState state;GenGeomGlslOptions glsl_opts;}geom;} ShaderModuleCacheKey;
/* SELECTOR */
static double now(void){
#ifdef _WIN32
 LARGE_INTEGER n,f;assert(QueryPerformanceFrequency(&f)&&QueryPerformanceCounter(&n));return (double)n.QuadPart/f.QuadPart;
#else
 struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static void save(const char *folder,const char *kind,unsigned version,unsigned smooth,unsigned provoking,const uint32_t *words,size_t n){
 char path[2048];snprintf(path,sizeof(path),"%s/%s-%u-%u-%u.spv",folder,kind,version,smooth,provoking);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(words,4,n,f)==n);assert(!fclose(f));
}
static GlslcResult compile(GlslcStage stage,const char *source,bool version){
 GlslcResult result;GlslcOptions opts={.spirv_1_6=version};if(!glslc_compile(stage,source,&opts,&result)){fprintf(stderr,"line %d: %s\n%s\n",result.error_line,result.error,source);abort();}return result;
}
#ifdef TEST_VULKAN
#include "spv-geom-vulkan.h"
#endif
#ifdef _WIN32
static void quiet_test_abort(int signal_number)
{
 (void)signal_number;
 ExitProcess(3); /* assertion/compiler error was already written to stderr */
}
static LONG WINAPI quiet_test_exception(EXCEPTION_POINTERS *exception)
{
 fprintf(stderr,"Shader test exception 0x%08lx at %p\n",
         (unsigned long)exception->ExceptionRecord->ExceptionCode,
         exception->ExceptionRecord->ExceptionAddress);
 fflush(stderr);
 ExitProcess(1);
 return EXCEPTION_EXECUTE_HANDLER;
}
#endif
static void quiet_test_errors(void)
{
#ifdef _WIN32
 SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS |
              SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
 _set_error_mode(_OUT_TO_STDERR);
 assert(signal(SIGABRT, quiet_test_abort) != SIG_ERR);
 SetUnhandledExceptionFilter(quiet_test_exception);
 /* Verify the policy without deliberately crashing a process. */
 assert(GetErrorMode() & SEM_NOGPFAULTERRORBOX);
 assert(signal(SIGABRT, quiet_test_abort) == quiet_test_abort);
#endif
}
int line_tests(int argc,char **argv){
 quiet_test_errors();
 assert(argc==2);unsigned cases=0;unsigned stages[]={1,8,16,32};
 for(unsigned primitive=0;primitive<=PRIM_TYPE_POLYGON;primitive++)for(unsigned front=0;front<3;front++)for(unsigned back=0;back<3;back++)
 for(unsigned smooth=0;smooth<2;smooth++)for(unsigned first=0;first<2;first++)for(unsigned wide=0;wide<2;wide++)for(unsigned vk=0;vk<2;vk++)for(unsigned version=0;version<2;version++)for(unsigned stage=0;stage<4;stage++){
  ShaderModuleCacheKey key={.kind=stages[stage],.geom={.state={.primitive_mode=primitive,.polygon_front_mode=front,.polygon_back_mode=back,.smooth_shading=smooth,.first_vertex_is_provoking=first,.wide_lines=wide,.z_perspective=true,.tri_rot0=2,.tri_rot1=1},.glsl_opts={.vulkan=vk}}};
  bool expected=stages[stage]==8&&vk&&!wide&&front==back&&((primitive>=PRIM_TYPE_LINES&&primitive<=PRIM_TYPE_LINE_STRIP)||(primitive==PRIM_TYPE_POLYGON&&front==POLY_MODE_LINE));
  pgraph_vk_spirv_1_6=version;SpvLineGeomOptions opts={0};assert(vk_module_line_options(&key,&opts)==expected);
  if(expected){assert(opts.smooth_shading==smooth&&opts.spirv_1_6==version);assert(opts.provoking_vertex==((primitive==PRIM_TYPE_POLYGON||first)?0u:1u));}cases++;
 }
 printf("Direct geometry selector: %u stage/primitive/mode/shading/wide/version cases passed\n",cases);
 SpvDirectModule invalid;allocations=0;assert(!pgraph_spv_line_geom((SpvLineGeomOptions){.provoking_vertex=2},&invalid));assert(!allocations&&!invalid.words&&!invalid.num_words);
 for(unsigned version=0;version<2;version++)for(unsigned smooth=0;smooth<2;smooth++)for(unsigned provoking=0;provoking<2;provoking++){
  GeomState state={.primitive_mode=PRIM_TYPE_LINES,.polygon_front_mode=POLY_MODE_FILL,.polygon_back_mode=POLY_MODE_FILL,.smooth_shading=smooth,.first_vertex_is_provoking=provoking==0};
  MString *glsl=pgraph_glsl_gen_geom(&state,(GenGeomGlslOptions){.vulkan=true});
  allocations=0;GlslcResult ref=compile(GLSLC_STAGE_GEOMETRY,glsl->str,version);unsigned ref_allocs=allocations;
  allocations=0;SpvDirectModule direct;assert(pgraph_spv_line_geom((SpvLineGeomOptions){smooth,provoking,version},&direct));assert(allocations==1);
  save(argv[1],"reference",version,smooth,provoking,ref.words,ref.num_words);save(argv[1],"direct",version,smooth,provoking,direct.words,direct.num_words);
  SpvDirectModule repeat;assert(pgraph_spv_line_geom((SpvLineGeomOptions){smooth,provoking,version},&repeat));assert(repeat.num_words==direct.num_words&&!memcmp(repeat.words,direct.words,direct.num_words*4));free(repeat.words);
  const unsigned iterations=512;double start=now();for(unsigned i=0;i<iterations;i++){GlslcResult m=compile(GLSLC_STAGE_GEOMETRY,glsl->str,version);free(m.words);}double ref_us=(now()-start)*1e6/iterations;
  start=now();for(unsigned i=0;i<iterations;i++){SpvDirectModule m;assert(pgraph_spv_line_geom((SpvLineGeomOptions){smooth,provoking,version},&m));free(m.words);}double direct_us=(now()-start)*1e6/iterations;
  printf("SPV %u smooth=%u provoking=%u: reference=%zu words/%u allocation calls/%.2fus; direct=%zu words/1 allocation/%.2fus\n",version,smooth,provoking,ref.num_words,ref_allocs,ref_us,direct.num_words,direct_us);
#ifdef TEST_VULKAN
  compare_on_gpu(argv[1],version,smooth,provoking,&ref,&direct);
#endif
  free(ref.words);free(direct.words);mstring_unref(glsl);
 }
#ifdef TEST_VULKAN
 finish_gpu();
#endif
 puts("Direct geometry: deterministic binary emission, one payload allocation, production reference generation passed");
 return 0;
}

#include "spv-triangle-tests.h"
#include "spv-quad-tests.h"
int main(int argc,char **argv){int result=line_tests(argc,argv);triangle_tests(argv[1]);quad_tests(argv[1]);return result;}

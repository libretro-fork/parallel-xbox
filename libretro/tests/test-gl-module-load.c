/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned GLenum,GLuint;
typedef int GLint;
#define GL_LINK_STATUS 1
#define GL_FALSE 0
#define GL_NO_ERROR 0
#define g_free free
typedef struct {uint32_t words[8];} ShaderModuleCacheKey;
typedef struct {ShaderModuleCacheKey key;GLuint gl_stage_program;} ShaderModuleCacheEntry;
typedef struct {unsigned unused;} PGRAPHGLState;
typedef struct {const void *data;size_t len;} XemuFileMap;
static const char *xemu_version="fixture-xemu",*shader_gl_vendor="fixture-vendor";
static unsigned char storage[2048],file_bytes[1024];
static size_t file_size,payload_offset,mapped_size;
static unsigned shift,created,loaded,queried,deleted,unlinked,unmapped;
static bool map_exists,mapped,linked,zero_program,load_error,query_error;
static GLenum error;
static char *module_get_dir(uint64_t hash){assert(hash==17);char *p=malloc(4);assert(p);memcpy(p,"dir",4);return p;}
static char *module_get_path(const char *dir,uint64_t hash){assert(!strcmp(dir,"dir")&&hash==17);char *p=malloc(6);assert(p);memcpy(p,"cache",6);return p;}
static bool xemu_file_map(XemuFileMap *m,const char *path){assert(!strcmp(path,"cache")&&!mapped);if(!map_exists)return false;memcpy(storage+shift,file_bytes,file_size);*m=(XemuFileMap){storage+shift,mapped_size};mapped=true;return true;}
static void xemu_file_unmap(XemuFileMap *m){assert(mapped&&m->data==storage+shift);mapped=false;unmapped++;}
static void qemu_unlink(const char *path){assert(!strcmp(path,"cache")&&!mapped);unlinked++;}
static GLuint glCreateProgram(void){assert(mapped);created++;return zero_program?0:123;}
static void glProgramBinary(GLuint p,GLenum fmt,const void *binary,int bytes){assert(mapped&&p==(zero_program?0:123)&&fmt==77&&bytes==16);assert(binary==storage+shift+payload_offset);for(int i=0;i<16;i++)assert(((const unsigned char*)binary)[i]==(unsigned char)(i*17));loaded++;if(load_error||!p)error=9;}
static void glGetProgramiv(GLuint p,GLenum parameter,GLint *value){assert(mapped&&p==(zero_program?0:123)&&parameter==GL_LINK_STATUS);queried++;*value=linked&&!zero_program;if(query_error||!p)error=10;}
static GLenum glGetError(void){GLenum r=error;error=0;return r;}
static void glDeleteProgram(GLuint p){assert(!mapped&&p==123);deleted++;}
/* PRODUCTION */
static void put(const void *p,size_t n){assert(file_size+n<=sizeof(file_bytes));memcpy(file_bytes+file_size,p,n);file_size+=n;}
static void string(const char *p){uint64_t n=strlen(p)+1;put(&n,8);put(p,n);}
static void build(const ShaderModuleCacheKey *key){file_size=0;string(xemu_version);string(shader_codegen_version);string(shader_gl_vendor);uint64_t n=sizeof(*key);put(&n,8);put(key,n);GLenum fmt=77;put(&fmt,sizeof(fmt));n=16;put(&n,8);payload_offset=file_size;for(unsigned i=0;i<16;i++){unsigned char b=i*17;put(&b,1);}}
int main(void){
 PGRAPHGLState r={0};ShaderModuleCacheKey key;for(unsigned i=0;i<8;i++)key.words[i]=0xabc00000+i;
 build(&key);size_t full=file_size;unsigned cases=0;
 for(shift=0;shift<16;shift++)for(unsigned mode=0;mode<32;mode++)for(size_t cut=0;cut<=full;cut++){
  ShaderModuleCacheEntry module={.key=key,.gl_stage_program=999};
  map_exists=true;mapped_size=cut;linked=mode&1;load_error=mode&2;query_error=mode&4;zero_program=mode&8;error=mode&16?42:0;
  created=loaded=queried=deleted=unlinked=unmapped=0;
  bool result=module_load_from_disk(&r,&module,17);
  bool accepted=cut==full&&linked&&!load_error&&!query_error&&!zero_program;
  assert(result==accepted&&!mapped&&unmapped==1);
  assert(module.gl_stage_program==(accepted?123:999));
  assert(created==(cut==full)&&loaded==created&&queried==created);
  assert(deleted==(!accepted&&created&&!zero_program)&&unlinked==!accepted);
  if(created)assert(!error);
  cases++;
 }
 for(shift=0;shift<16;shift++){
  ShaderModuleCacheEntry module={.key=key,.gl_stage_program=999};
  map_exists=false;created=unmapped=unlinked=0;assert(!module_load_from_disk(&r,&module,17));assert(!created&&!unmapped&&!unlinked);
  map_exists=true;mapped_size=full;module.key.words[3]^=1;created=loaded=queried=deleted=unlinked=unmapped=0;
  assert(!module_load_from_disk(&r,&module,17)&&module.gl_stage_program==999);assert(!created&&!loaded&&!queried&&!deleted&&unlinked==1&&unmapped==1);
 }
 printf("PASS: %u mapped cache admissions; truncations/alignment, link/API rejection, stale errors, exact borrowed binary input and rejected-program cleanup\n",cases);
}

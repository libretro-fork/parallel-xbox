#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production GL module-binary saving: scratch reuse, exact bytes and error lifetimes."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 and sys.argv[2]!='--gl' else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text();parts=[]
for name in ['module_binary_reserve','module_write_to_disk','module_save_to_disk']:
 m=re.search(r'^static (?:void \*|void|bool) *'+name+r'\([^;{]*\)\s*\{.*?^\}',s,re.M|re.S);assert m,name
 parts.append(m.group())
parts.insert(1,re.search(r'typedef struct ModuleDiskSave \{.*?\} ModuleDiskSave;',s,re.S)[0])
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#ifdef _WIN32
#include <process.h>
static char temporary_path[4096];
#endif
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define MAX(a,b) ((a)>(b)?(a):(b))
#ifdef REAL_GL
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#else
#define GL_PROGRAM_BINARY_LENGTH 1
#define GL_NO_ERROR 0
typedef int GLint;
typedef int GLsizei;
typedef unsigned GLenum;
#endif
typedef struct {uint32_t words[12];} ShaderModuleCacheKey;
typedef struct {unsigned gl_stage_program;ShaderModuleCacheKey key;} ShaderModuleCacheEntry;
typedef struct GLDiskSlot {void *scratch;size_t capacity;void *data;void(*write)(struct GLDiskSlot *);const char *base_path,*vendor;} GLDiskSlot;
typedef struct {GLDiskSlot slot;bool busy;} GLDiskWriter;
typedef struct {GLDiskWriter *cache_writer;} PGRAPHGLState;
static bool defer_write;
static GLDiskSlot *gl_disk_writer_acquire(GLDiskWriter *w){return w&&!w->busy?&w->slot:NULL;}
static void finish_write(GLDiskWriter *w){w->slot.write(&w->slot);w->busy=false;}
static void gl_disk_writer_submit(GLDiskWriter *w){w->busy=true;if(!defer_write)finish_write(w);}
#define g_new(t,n) FORBIDDEN_PER_SAVE_METADATA_ALLOCATION
static unsigned allocations,exports,opens,unlinks,queries;
static int query_size=100,copied_size=75,gl_error;
static GLenum expected_format=0x12345678;static void *expected_binary;
static bool fail_open,fail_write;
static uint8_t captured[200000];static size_t captured_len;
static const char *shader_gl_vendor="fixture vendor";
static void *g_malloc(size_t n) {allocations++;void *p=malloc(n);assert(p);return p;}
static void g_free(void *p) {free(p);}
#ifndef REAL_GL
static void glGetProgramiv(unsigned program,unsigned what,GLint *value) {assert(program==7 && what==GL_PROGRAM_BINARY_LENGTH);queries++;*value=query_size;}
static void glGetProgramBinary(unsigned program,GLsizei size,GLsizei *copied,GLenum *format,void *data) {
 assert(program==7 && size==query_size && copied_size<=size);exports++;
 memset(data,0x5a,copied_size);*copied=copied_size;*format=0x12345678;
}
static GLenum glGetError(void) {return gl_error;}
#endif
static FILE *fopen_utf8(const char *path,const char *mode) {
 assert(!strcmp(path,"module.cache") && !strcmp(mode,"wb"));opens++;if(fail_open)return NULL;
#ifdef _WIN32
 const char *root=getenv("TEMP");assert(root);
 snprintf(temporary_path,sizeof(temporary_path),"%s/gl-module-export-%u.tmp",root,(unsigned)_getpid());
 FILE *f=fopen(temporary_path,"w+b");assert(f);return f;
#else
 FILE *f=tmpfile();assert(f);return f;
#endif
}
static void qemu_unlink(const char *path) {assert(!strcmp(path,"module.cache"));unlinks++;}
static size_t write_bytes(const void *data,size_t size,size_t count,FILE *file) {return fail_write?0:fwrite(data,size,count,file);}
static int close_capture(FILE *file) {
 assert(!fflush(file) && !fseek(file,0,SEEK_END));long n=ftell(file);assert(n>=0 && n<(long)sizeof(captured));
 rewind(file);captured_len=n;assert(fread(captured,1,n,file)==(size_t)n);int result=fclose(file);
#ifdef _WIN32
 remove(temporary_path);
#endif
 return result;
}
#define fwrite write_bytes
#define fclose close_capture
static bool shader_write_cache_version(FILE *file) {const uint32_t magic=0xaabbccdd;return fwrite(&magic,4,1,file)==1;}
static FILE *gl_cache_file_begin(GLDiskSlot *slot,const char *family,uint64_t hash){(void)slot;(void)family;(void)hash;return fopen_utf8("module.cache","wb");}
static bool gl_cache_file_finish(GLDiskSlot *slot,FILE *f,bool ok){(void)slot;ok=fclose(f)==0&&ok;if(!ok)qemu_unlink("module.cache");return ok;}
/* PRODUCTION */
static void check_blob(ShaderModuleCacheEntry *module) {
 size_t p=0;uint32_t magic,format;uint64_t len;
 memcpy(&magic,captured+p,4);p+=4;assert(magic==0xaabbccdd);
 memcpy(&len,captured+p,8);p+=8;assert(len==strlen(shader_gl_vendor)+1);
 assert(!memcmp(captured+p,shader_gl_vendor,len));p+=len;
 memcpy(&len,captured+p,8);p+=8;assert(len==sizeof(module->key));
 assert(!memcmp(captured+p,&module->key,len));p+=len;
 memcpy(&format,captured+p,4);p+=4;assert(format==expected_format);
 memcpy(&len,captured+p,8);p+=8;assert(len==(unsigned)copied_size && p+len==captured_len);
 if(expected_binary)assert(!memcmp(captured+p,expected_binary,len));
 else for(size_t i=0;i<len;i++)assert(captured[p+i]==0x5a);
}
#ifndef REAL_GL
int main(void) {
 ModuleDiskSave metadata;
 GLDiskWriter w={.slot={.data=&metadata,.base_path="fixture",.vendor=shader_gl_vendor}};
 PGRAPHGLState r={.cache_writer=&w};ShaderModuleCacheEntry module={.gl_stage_program=7};
 for(unsigned i=0;i<12;i++)module.key.words[i]=i*17;
 module_binary_reserve(&w.slot,65536);assert(allocations==1);
 void *first=w.slot.scratch;
 for(unsigned i=0;i<100;i++) {module.key.words[0]=i;module_save_to_disk(&r,&module,i);check_blob(&module);assert(w.slot.scratch==first && allocations==1);}
 unsigned before=exports;query_size=0;module_save_to_disk(&r,&module,0);query_size=-1;module_save_to_disk(&r,&module,0);assert(exports==before && allocations==1);
 query_size=70000;copied_size=69999;module_save_to_disk(&r,&module,0);check_blob(&module);assert(allocations==2 && w.slot.capacity==131072);
 query_size=100000;copied_size=500;module_save_to_disk(&r,&module,0);check_blob(&module);assert(allocations==2);
 gl_error=9;before=opens;module_save_to_disk(&r,&module,0);assert(opens==before && allocations==2);gl_error=0;
 fail_open=true;module_save_to_disk(&r,&module,0);fail_open=false;assert(allocations==2);
 fail_write=true;module_save_to_disk(&r,&module,0);assert(unlinks==1);fail_write=false;
 module_save_to_disk(&r,&module,0);check_blob(&module);assert(allocations==2);
 defer_write=true;module_save_to_disk(&r,&module,0);
 ShaderModuleCacheEntry expected=module;module.key.words[0]^=0xffffffff;
 before=exports;unsigned query_before=queries;assert(!module_save_to_disk(&r,&module,1)&&exports==before&&queries==query_before);
 finish_write(&w);check_blob(&expected);defer_write=false;
 free(w.slot.scratch);w.slot.scratch=NULL;w.slot.capacity=0;
 module_binary_reserve(&w.slot,65536);query_size=100;copied_size=75;module_save_to_disk(&r,&module,0);check_blob(&module);assert(allocations==3);free(w.slot.scratch);w.slot.scratch=NULL;w.slot.capacity=0;
 puts("GL module export: 100 same-buffer saves, growth, exact header/payload bytes, driver/open/write errors and renderer reinitialization PASS");
}
#else
/* REAL GL */
#endif
'''.replace('/* PRODUCTION */','\n'.join(parts))
code=code.replace('/* REAL GL */',(r/'libretro/tests/gl-module-export-real.h').read_text())
with tempfile.TemporaryDirectory(prefix='gl-module-export-') as tmp:
    d=Path(tmp);c=d/'test.c';c.write_text(code)
    exe=Path(os.environ.get('GL_MODULE_EXPORT_BINARY',str(d/('test.exe' if os.name=='nt' else 'test'))))
    env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
    subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('GL_MODULE_EXPORT_CFLAGS','')),str(c),'-o',str(exe)],check=True,env=env)
    if not os.environ.get('GL_MODULE_EXPORT_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env,timeout=30)
    if '--gl' in sys.argv:
        subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-DREAL_GL',str(c),'-lEGL','-lGL','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=30,env=dict(env,EGL_PLATFORM='surfaceless'))

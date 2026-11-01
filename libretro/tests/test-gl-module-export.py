#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production GL module-binary saving: scratch reuse, exact bytes and error lifetimes."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text();parts=[]
for name in ['module_binary_reserve','module_binary_release','module_save_to_disk']:
 m=re.search(r'^static (?:void \*|void) *'+name+r'\([^;{]*\)\s*\{.*?^\}',s,re.M|re.S);assert m,name
 parts.append(m.group())
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define GL_PROGRAM_BINARY_LENGTH 1
#define GL_NO_ERROR 0
typedef int GLint;
typedef int GLsizei;
typedef unsigned GLenum;
typedef struct {uint32_t words[12];} ShaderModuleCacheKey;
typedef struct {unsigned gl_stage_program;ShaderModuleCacheKey key;} ShaderModuleCacheEntry;
typedef struct {void *module_binary_scratch;size_t module_binary_capacity;} PGRAPHGLState;
static unsigned allocations,exports,opens,unlinks;
static int query_size=100,copied_size=75,gl_error;
static bool fail_open,fail_write;
static uint8_t captured[200000];static size_t captured_len;
static const char *shader_gl_vendor="fixture vendor";
static void *g_malloc(size_t n) {allocations++;void *p=malloc(n);assert(p);return p;}
static void g_free(void *p) {free(p);}
static char *g_strdup_printf(const char *fmt,...) {
 va_list ap,copy;va_start(ap,fmt);va_copy(copy,ap);int n=vsnprintf(NULL,0,fmt,copy);va_end(copy);
 assert(n>=0);char *p=malloc(n+1);assert(p);vsnprintf(p,n+1,fmt,ap);va_end(ap);return p;
}
static void glGetProgramiv(unsigned program,unsigned what,GLint *value) {assert(program==7 && what==GL_PROGRAM_BINARY_LENGTH);*value=query_size;}
static void glGetProgramBinary(unsigned program,GLsizei size,GLsizei *copied,GLenum *format,void *data) {
 assert(program==7 && size==query_size && copied_size<=size);exports++;
 memset(data,0x5a,copied_size);*copied=copied_size;*format=0x12345678;
}
static GLenum glGetError(void) {return gl_error;}
static const char *xemu_settings_get_base_path(void) {return "fixture";}
static void qemu_mkdir(const char *path) {assert(path);}
static char *module_get_dir(uint64_t hash) {(void)hash;return g_strdup_printf("directory");}
static char *module_get_path(const char *dir,uint64_t hash) {(void)dir;(void)hash;return g_strdup_printf("module.cache");}
static FILE *qemu_fopen(const char *path,const char *mode) {assert(!strcmp(path,"module.cache") && !strcmp(mode,"wb"));opens++;return fail_open?NULL:tmpfile();}
static void qemu_unlink(const char *path) {assert(!strcmp(path,"module.cache"));unlinks++;}
static size_t write_bytes(const void *data,size_t size,size_t count,FILE *file) {return fail_write?0:fwrite(data,size,count,file);}
static int close_capture(FILE *file) {
 assert(!fflush(file) && !fseek(file,0,SEEK_END));long n=ftell(file);assert(n>=0 && n<(long)sizeof(captured));
 rewind(file);captured_len=n;assert(fread(captured,1,n,file)==(size_t)n);return fclose(file);
}
#define fwrite write_bytes
#define fclose close_capture
static bool shader_write_cache_version(FILE *file) {const uint32_t magic=0xaabbccdd;return fwrite(&magic,4,1,file)==1;}
/* PRODUCTION */
static void check_blob(ShaderModuleCacheEntry *module) {
 size_t p=0;uint32_t magic,format;uint64_t len;
 memcpy(&magic,captured+p,4);p+=4;assert(magic==0xaabbccdd);
 memcpy(&len,captured+p,8);p+=8;assert(len==strlen(shader_gl_vendor)+1);
 assert(!memcmp(captured+p,shader_gl_vendor,len));p+=len;
 memcpy(&len,captured+p,8);p+=8;assert(len==sizeof(module->key));
 assert(!memcmp(captured+p,&module->key,len));p+=len;
 memcpy(&format,captured+p,4);p+=4;assert(format==0x12345678);
 memcpy(&len,captured+p,8);p+=8;assert(len==(unsigned)copied_size && p+len==captured_len);
 for(size_t i=0;i<len;i++)assert(captured[p+i]==0x5a);
}
int main(void) {
 PGRAPHGLState r={0};ShaderModuleCacheEntry module={.gl_stage_program=7};
 for(unsigned i=0;i<12;i++)module.key.words[i]=i*17;
 module_binary_reserve(&r,65536);assert(allocations==1);
 void *first=r.module_binary_scratch;
 for(unsigned i=0;i<100;i++) {module.key.words[0]=i;module_save_to_disk(&r,&module,i);check_blob(&module);assert(r.module_binary_scratch==first && allocations==1);}
 unsigned before=exports;query_size=0;module_save_to_disk(&r,&module,0);query_size=-1;module_save_to_disk(&r,&module,0);assert(exports==before && allocations==1);
 query_size=70000;copied_size=69999;module_save_to_disk(&r,&module,0);check_blob(&module);assert(allocations==2 && r.module_binary_capacity==131072);
 query_size=100000;copied_size=500;module_save_to_disk(&r,&module,0);check_blob(&module);assert(allocations==2);
 gl_error=9;before=opens;module_save_to_disk(&r,&module,0);assert(opens==before && allocations==2);gl_error=0;
 fail_open=true;module_save_to_disk(&r,&module,0);fail_open=false;assert(allocations==2);
 fail_write=true;module_save_to_disk(&r,&module,0);assert(unlinks==1);fail_write=false;
 module_save_to_disk(&r,&module,0);check_blob(&module);assert(allocations==2);
 module_binary_release(&r);assert(!r.module_binary_scratch && !r.module_binary_capacity);
 module_binary_reserve(&r,65536);query_size=100;copied_size=75;module_save_to_disk(&r,&module,0);check_blob(&module);assert(allocations==3);module_binary_release(&r);
 puts("GL module export: 100 same-buffer saves, growth, exact header/payload bytes, driver/open/write errors and renderer reinitialization PASS");
}
'''.replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='gl-module-export-') as tmp:
 d=Path(tmp);c=d/'test.c';c.write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('GL_MODULE_EXPORT_CFLAGS','')),str(c),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)

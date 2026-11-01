# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production GL binary-cache version I/O without a graphics device."""
from pathlib import Path
import os, shlex
import re
import subprocess
import sys
import tempfile

repo=Path(sys.argv[1]).resolve()
s=(repo/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
parts=[re.search(r'static const char shader_codegen_version\[\] = .*?;',s)[0],
       re.search(r'typedef struct FileCursor \{.*?\} FileCursor;',s,re.S)[0]]
for name in ('cursor_take','cursor_take_u64','cursor_take_string_eq','shader_read_cache_version',
             'shader_write_cache_version','module_read_header_view','module_read_header'):
    m=re.search(r'^static (?:const void \*|bool )'+name+r'\([^;{]*\)\s*\{',s,re.M)
    assert m,name
    end=s.index('{',m.start())+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    parts.append(s[m.start():end])
code=r'''
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
static const char *xemu_version="0.8.120",*shader_gl_vendor="test vendor";
typedef struct {uint64_t state[256];} ShaderModuleCacheKey;
/* PRODUCTION */
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
static FILE *test_file(void) {
  char dir[MAX_PATH],path[MAX_PATH];assert(GetTempPathA(MAX_PATH,dir));
  assert(GetTempFileNameA(dir,"glk",0,path));
  int fd=_open(path,_O_RDWR|_O_BINARY|_O_TEMPORARY);assert(fd>=0);
  FILE *f=_fdopen(fd,"w+b");assert(f);return f;
}
#else
static FILE *test_file(void) {return tmpfile();}
#endif
static void string(FILE *f,const char *s){uint64_t n=strlen(s)+1;assert(fwrite(&n,8,1,f)==1);assert(fwrite(s,n,1,f)==1);}
int main(void){
  FILE *f=test_file();assert(f);assert(shader_write_cache_version(f));long header=ftell(f);assert(header>0);
  string(f,shader_gl_vendor);ShaderModuleCacheKey original={{1,2,3,4}},read;
  uint64_t size=sizeof(original);assert(fwrite(&size,8,1,f)==1);assert(fwrite(&original,size,1,f)==1);
  long total=ftell(f);rewind(f);uint8_t data[4096];assert(total<(long)sizeof(data));assert(fread(data,1,total,f)==(size_t)total);fclose(f);
  FileCursor c={data,header};assert(shader_read_cache_version(&c) && !c.left);
  c=(FileCursor){data,total};assert(module_read_header(&c,&read) && !c.left);assert(!memcmp(&read,&original,sizeof(read)));
  for(unsigned shift=0;shift<32;shift++) {
    uint8_t unaligned[4096];memcpy(unaligned+shift,data,total);
    c=(FileCursor){unaligned+shift,total};
    const void *view=module_read_header_view(&c);
    assert(view==unaligned+shift+total-sizeof(original) && !c.left);
    assert(!memcmp(view,&original,sizeof(original)));
    for(long n=0;n<total;n++) {
      c=(FileCursor){unaligned+shift,n};assert(!module_read_header_view(&c));
      c=(FileCursor){unaligned+shift,n};assert(!module_read_header(&c,&read));
    }
  }
  for(long n=0;n<header;n++){c=(FileCursor){data,n};assert(!shader_read_cache_version(&c));}
  data[8]^=1;c=(FileCursor){data,total};assert(!module_read_header(&c,&read));data[8]^=1;
  data[header-2]^=1;c=(FileCursor){data,total};assert(!module_read_header(&c,&read));data[header-2]^=1;
  /* Old caches place the GL vendor immediately after xemu_version. */
  f=test_file();assert(f);string(f,xemu_version);string(f,shader_gl_vendor);size=ftell(f);rewind(f);assert(fread(data,1,size,f)==size);fclose(f);
  c=(FileCursor){data,size};assert(!shader_read_cache_version(&c));
  puts("GL shader cache version: owned/borrowed key round trips, 32 alignments, legacy/changed versions and every truncated prefix passed");
}
'''.replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='gl-shader-version-') as directory:
    p=Path(directory);(p/'test.c').write_text(code)
    binary=os.environ.get('GL_SHADER_VERSION_BINARY',str(p/'test'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')+['-O2','-std=gnu11','-Wall','-Wextra','-Werror']+
                   shlex.split(os.environ.get('GL_SHADER_VERSION_CFLAGS','-fsanitize=address,undefined'))+
                   [str(p/'test.c'),'-o',binary],check=True)
    if not os.environ.get('GL_SHADER_VERSION_SKIP_RUN'):
        subprocess.run([binary],check=True,timeout=30)

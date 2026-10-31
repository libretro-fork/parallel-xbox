# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production GL binary-cache version I/O without a graphics device."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo=Path(sys.argv[1]).resolve()
s=(repo/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
parts=[re.search(r'static const char shader_codegen_version\[\] = .*?;',s)[0],
       re.search(r'typedef struct FileCursor \{.*?\} FileCursor;',s,re.S)[0]]
for name in ('cursor_take','cursor_take_u64','cursor_take_string_eq','shader_read_cache_version',
             'shader_write_cache_version','module_read_header'):
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
typedef struct {uint64_t state[4];} ShaderModuleCacheKey;
/* PRODUCTION */
static void string(FILE *f,const char *s){uint64_t n=strlen(s)+1;assert(fwrite(&n,8,1,f)==1);assert(fwrite(s,n,1,f)==1);}
int main(void){
  FILE *f=tmpfile();assert(f);assert(shader_write_cache_version(f));long header=ftell(f);assert(header>0);
  string(f,shader_gl_vendor);ShaderModuleCacheKey original={{1,2,3,4}},read;
  uint64_t size=sizeof(original);assert(fwrite(&size,8,1,f)==1);assert(fwrite(&original,size,1,f)==1);
  long total=ftell(f);rewind(f);uint8_t data[1024];assert(total<(long)sizeof(data));assert(fread(data,1,total,f)==(size_t)total);fclose(f);
  FileCursor c={data,header};assert(shader_read_cache_version(&c) && !c.left);
  c=(FileCursor){data,total};assert(module_read_header(&c,&read) && !c.left);assert(!memcmp(&read,&original,sizeof(read)));
  for(long n=0;n<total;n++){c=(FileCursor){data,n};assert(!module_read_header(&c,&read));}
  for(long n=0;n<header;n++){c=(FileCursor){data,n};assert(!shader_read_cache_version(&c));}
  data[8]^=1;c=(FileCursor){data,total};assert(!module_read_header(&c,&read));data[8]^=1;
  data[header-2]^=1;c=(FileCursor){data,total};assert(!module_read_header(&c,&read));data[header-2]^=1;
  /* Old caches place the GL vendor immediately after xemu_version. */
  f=tmpfile();assert(f);string(f,xemu_version);string(f,shader_gl_vendor);size=ftell(f);rewind(f);assert(fread(data,1,size,f)==size);fclose(f);
  c=(FileCursor){data,size};assert(!shader_read_cache_version(&c));
  puts("GL shader cache version: current header/key round trip, legacy/changed versions and every truncated prefix passed");
}
'''.replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='gl-shader-version-') as directory:
    p=Path(directory);(p/'test.c').write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                    '-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=30)

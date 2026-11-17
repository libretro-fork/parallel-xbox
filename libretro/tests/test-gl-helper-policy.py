# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual emitter policy and binary-cache revision, including old-file rejection."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
psh=(root/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text()
shader=(root/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
header=(root/'hw/xbox/nv2a/pgraph/glsl/psh.h').read_text()
def function(source,signature):
 a=source.index(signature);b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
policy=function(psh,'static bool psh_preserve_helpers(')
cursor=re.search(r'typedef struct FileCursor \{.*?\} FileCursor;',shader,re.S)[0]
cache='\n'.join(function(shader,x) for x in ['static const void *cursor_take(', 'static bool cursor_take_u64(', 'static bool cursor_take_string_eq(', 'static bool shader_read_cache_version(', 'static bool shader_write_cache_version('])
version=re.search(r'static const char shader_codegen_version\[\] = "[^"]+";',shader)[0]
code=r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define CHECK(x) do{if(!(x)){fprintf(stderr,"HELPER_POLICY_FAIL line=%u: %s\n",__LINE__,#x);exit(1);}}while(0)
struct PixelShader {bool ready;GenPshGlslOptions opts;const PshState *state;};
/* POLICY */
/* CURSOR */
static const char xemu_version[]="fixture-upstream";
/* VERSION */
/* CACHE */
static size_t put(uint8_t *out,size_t offset,const char *text){
 uint64_t n=strlen(text)+1;memcpy(out+offset,&n,8);memcpy(out+offset+8,text,n);return offset+8+n;
}
int main(void){
 bool defaults=false;
#ifdef PGRAPH_GLSL_GL_DEFAULT_HELPERS
 _Static_assert(PGRAPH_GLSL_GL_DEFAULT_HELPERS==1,"Disabled advertised helper contract");
 defaults=true;CHECK(strcmp(shader_codegen_version,"ordered-light-range-2"));
#endif
 unsigned policies=0,versions=0;
 for(unsigned ready=0;ready<2;ready++)for(unsigned vk=0;vk<2;vk++)for(unsigned key=0;key<1024;key++){
  PshState state={.depth_format=key},before=state;struct PixelShader ps={.ready=ready,.opts.vulkan=vk,.state=&state};
  CHECK(psh_preserve_helpers(&ps)==((defaults&&!vk)||ready||!!(key&512)));
  CHECK(!memcmp(&state,&before,sizeof(state)));policies++;
 }
 uint8_t bytes[512];FILE *file=tmpfile();CHECK(file&&shader_write_cache_version(file));
 long length=ftell(file);CHECK(length>0&&length<(long)sizeof(bytes));rewind(file);CHECK(fread(bytes,length,1,file)==1);CHECK(!fclose(file));
 FileCursor whole={bytes,(size_t)length};CHECK(shader_read_cache_version(&whole)&&!whole.left);versions++;
 for(size_t n=0;n<(size_t)length;n++){FileCursor truncated={bytes,n};CHECK(!shader_read_cache_version(&truncated));versions++;}
 const char *upstreams[]={xemu_version,"other-upstream",xemu_version,xemu_version};
 const char *generators[]={shader_codegen_version,shader_codegen_version,"ordered-light-range-2","future-generator"};
 for(unsigned i=0;i<4;i++){
  size_t n=put(bytes,0,upstreams[i]);n=put(bytes,n,generators[i]);FileCursor cursor={bytes,n};
  bool expected=!strcmp(upstreams[i],xemu_version)&&!strcmp(generators[i],shader_codegen_version);
  CHECK(shader_read_cache_version(&cursor)==expected);if(expected)CHECK(!cursor.left);versions++;
 }
 printf("PASS helper policy: %u state/backend/ready cases, %u cache-version/truncation cases, defaultGL=%u; Vulkan retains keyed semantics\n",policies,versions,defaults);
}
""".replace('/* POLICY */',policy).replace('/* CURSOR */',cursor).replace('/* VERSION */',version).replace('/* CACHE */',cache)
with tempfile.TemporaryDirectory(prefix='gl-helper-policy-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(root),str(c),'-o',str(exe)]
 c.write_text(code);subprocess.run(command,check=True,env=env);subprocess.run([str(exe)],check=True,env=env,timeout=30)
 mutations=[('ignored generator revision','cursor_take_string_eq(c, shader_codegen_version)','true'),('lost explicit key','(ps->state->depth_format & DEPTH_FORMAT_HELPER_LIVENESS)','false'),('lost ready mode','ps->ready ||','false ||')]
 if 'PGRAPH_GLSL_GL_DEFAULT_HELPERS' in header:
  mutations += [('lost default GL helpers','(PGRAPH_GLSL_GL_DEFAULT_HELPERS && !ps->opts.vulkan)','false'),('changed unkeyed Vulkan','!ps->opts.vulkan','true'),('stale binary version',version,'static const char shader_codegen_version[] = "ordered-light-range-2";')]
 for name,old,new in mutations:
  assert old in code,name;c.write_text(code.replace(old,new,1));subprocess.run(command,check=True,env=env)
  bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
  assert bad.returncode==1 and b'HELPER_POLICY_FAIL' in bad.stderr,(name,bad.returncode,bad.stderr)
  print('Required helper-policy fault rejected:',name,flush=True)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Dynamic vertex families retain their owner until both prefixes fit."""
from pathlib import Path
import os, re, subprocess, sys, tempfile

root = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
source = (root/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
header = (root/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text()
key = re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;', header, re.S).group()
begin = source.index('#define GEN_JOBS 16')
end = source.index('static retro_eventcount_t gen_ready;', begin)
records = source[begin:end]

def function(name):
    start = source.index('static bool '+name+'(')
    end = source.index('{', start)+1
    depth = 1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}')
        end += 1
    return source[start:end]

begin = source.index('    while (gen_jobs_have_free(2))', source.index('/* ...and dynamic vertex families'))
end = source.index('    /* ...and cold-boot seed keys', begin)
feed = source[begin:end]
code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned GLenum;
#define GL_VERTEX_SHADER 0x8b31u
#define GL_FRAGMENT_SHADER 0x8b30u
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL GL dynamic pair line=%d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
typedef int retro_atomic_int_t;
/* KEY */
/* RECORDS */
static unsigned published;
static void retro_atomic_store_release_int(int *p,int value){CHECK(value==GEN_QUEUED);*p=value;published++;}
/* FUNCTIONS */
static VshState bank[8];
static unsigned taken,pending;
bool pgraph_glsl_vsh_dynamic_pending_take(VshState *out){
 if(taken==pending)return false;
 *out=bank[taken++];return true;
}
static bool feed(void){bool fed=false;
/* FEED */
 return fed;
}
static void make_key(ShaderModuleCacheKey *key,unsigned family,unsigned prefix){
 memset(key,0,sizeof(*key));key->kind=GL_VERTEX_SHADER;
 key->vsh.state.uniform_attrs=family+1;
 key->vsh.state.programmable.program_length=1;
 key->vsh.state.programmable.program_data[0][0]=0x68429u+family;
 key->vsh.glsl_opts.prefix_outputs=prefix;
}
static void check_families(unsigned count){
 for(unsigned family=0;family<count;family++)for(unsigned prefix=0;prefix<2;prefix++){
  ShaderModuleCacheKey key;make_key(&key,family,prefix);unsigned matches=0;
  for(unsigned i=0;i<GEN_JOBS;i++)if(gen_jobs[i].used&&!memcmp(&gen_jobs[i].key,&key,sizeof(key))){
   CHECK(gen_jobs[i].state==GEN_QUEUED&&!gen_jobs[i].gate&&!gen_jobs[i].seed&&!gen_jobs[i].code);matches++;
  }
  CHECK(matches==1);
 }
}
int main(void){
 unsigned cases=0;
 for(unsigned mask=0;mask<(1u<<GEN_JOBS);mask++)for(unsigned n=1;n<=8;n+=7){
  memset(gen_jobs,0,sizeof(gen_jobs));taken=published=0;pending=n;
  for(unsigned family=0;family<n;family++){ShaderModuleCacheKey key;make_key(&key,family,0);bank[family]=key.vsh.state;}
  unsigned free_slots=0;
  for(unsigned i=0;i<GEN_JOBS;i++){
   if(mask&(1u<<i)){
    gen_jobs[i].used=true;gen_jobs[i].key.kind=GL_FRAGMENT_SHADER;
    gen_jobs[i].key.psh.state.combiner_control=i+1;
    gen_jobs[i].state=(i&1)?GEN_COMPLETE:GEN_QUEUED;
    gen_jobs[i].code=(MString *)(uintptr_t)(i+1);
   }else free_slots++;
  }
  for(unsigned need=1;need<=GEN_JOBS;need++)CHECK(gen_jobs_have_free(need)==(free_slots>=need));
  unsigned accepted=free_slots/2<n?free_slots/2:n;
  CHECK(feed()==(accepted!=0));CHECK(taken==accepted&&published==accepted*2);
  /* Consumed bank records can change: immutable published keys own their bytes. */
  memset(bank,0xa5,accepted*sizeof(*bank));check_families(accepted);
  for(unsigned i=0;i<GEN_JOBS;i++)if(mask&(1u<<i)){
   CHECK(gen_jobs[i].used&&gen_jobs[i].key.kind==GL_FRAGMENT_SHADER&&
         gen_jobs[i].key.psh.state.combiner_control==i+1&&
         gen_jobs[i].state==((i&1)?GEN_COMPLETE:GEN_QUEUED)&&
         gen_jobs[i].code==(MString *)(uintptr_t)(i+1));
   gen_jobs[i].used=false;gen_jobs[i].code=NULL;gen_jobs[i].state=GEN_IDLE;
  }
  CHECK(feed()==(accepted<n));CHECK(taken==n&&published==n*2);
  memset(bank,0x5a,sizeof(bank));check_families(n);cases++;
 }
 printf("GL dynamic pairs: %u complete occupancy/backpressure/retry/ownership cases, actual key layout PASS\n",cases);
}
'''.replace('/* KEY */',key).replace('/* RECORDS */',records).replace(
    '/* FUNCTIONS */',function('gen_job_push')+'\n'+function('gen_jobs_have_free')).replace('/* FEED */',feed)
with tempfile.TemporaryDirectory(prefix='gl-dynamic-pair-') as temporary:
    folder=Path(temporary);(folder/'qemu').mkdir()
    (folder/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
    (folder/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
    c=folder/'test.c';exe=folder/('test.exe' if os.name=='nt' else 'test')
    command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
             '-fsanitize-undefined-trap-on-error','-I'+str(folder),'-I'+str(root),str(c),'-o',str(exe)]
    c.write_text(code);subprocess.run(command,check=True);subprocess.run([str(exe)],check=True,timeout=90)
    old='while (gen_jobs_have_free(2))'
    assert old in code
    c.write_text(code.replace(old,'while (gen_jobs_have_free(1))',1))
    subprocess.run(command,check=True)
    bad=subprocess.run([str(exe)],capture_output=True,timeout=90)
    assert bad.returncode==1 and b'FAIL GL dynamic pair' in bad.stderr
    print('Required negative control rejected: one-slot family dequeue loses a prefix')

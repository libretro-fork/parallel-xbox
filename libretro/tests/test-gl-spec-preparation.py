#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Full specialization records survive until their exact stage keys are owned."""
from pathlib import Path
import os, re, subprocess, sys, tempfile

root = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
source = (root/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
header = (root/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text()
geom = (root/'hw/xbox/nv2a/pgraph/glsl/geom.c').read_text()
key = re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;', header, re.S).group()


def function(text, name):
    start = text.index(name)
    end = text.index('{', start)+1
    depth = 1
    while depth:
        depth += (text[end] == '{')-(text[end] == '}')
        end += 1
    return text[start:end]


a = source.index('#define GEN_JOBS 16')
b = source.index('static retro_eventcount_t gen_ready;', a)
records = source[a:b]
a = source.index('    /* 2) Feed the worker:')
b = source.index('    /* ...and dynamic vertex families', a)
feed = source[a:b]
# Compile both production draw predicates, not copies of the expected policy.
draw_conditions=[]
for variable, local in (('t','state'),('ts','state')):
    begin=source.index('if ('+variable+' >= 0 &&')+4
    end=source.index(') {',begin)
    predicate=source[begin:end]
    draw_conditions.append('static bool draw_ready_'+variable+'(PGRAPHGLState *r,int '+variable+',ShaderState '+local+'){return '+predicate+';}')
code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/qemu/queue.h"
typedef unsigned GLenum;
#define GL_VERTEX_SHADER 0x8b31
#define GL_FRAGMENT_SHADER 0x8b30
#define GL_GEOMETRY_SHADER 0x8dd9
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*(x)))
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL GL specialization preparation line=%d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
/* KEY */
/* RECORDS */
static unsigned published,dynamic_pending,notified,take_calls;
static struct {bool used,gate;} comb_async[32];
typedef struct LruNode {QTAILQ_ENTRY(LruNode) next_bin;uint64_t hash;} LruNode;
typedef struct {QTAILQ_HEAD(,LruNode) bins[4];} Lru;
typedef struct {LruNode node;ShaderModuleCacheKey key;} ShaderModuleCacheEntry;
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef struct {ShaderState spec_pending[4];unsigned spec_pending_r,spec_pending_w;Lru shader_module_cache;struct {int phase;} spec_track[1];} PGRAPHGLState;
#define SPEC_ADOPTED 2
#define SPEC_PIPE_RESERVED 3
static ShaderModuleCacheEntry cached[6];
static unsigned cached_n;
static unsigned lru_hash_to_bin(Lru *cache,uint64_t hash){(void)cache;return hash&3u;}
/* Force collisions across every stage/family. The production key predicate
 * must still reject nonmatching entries. */
static uint64_t xxh3_64(const void *key,size_t size, uint64_t seed __attribute__((unused))){(void)key;CHECK(size==sizeof(ShaderModuleCacheKey));return 0x42;}
static void fixture_publish(retro_atomic_int_t *p,int value){CHECK(value==GEN_QUEUED);retro_atomic_store_release_int(p,value);published++;}
/* JOB FUNCTIONS */
/* GEOM */
/* KEYS */
/* KNOWN */
/* DRAW CONDITIONS */
bool pgraph_glsl_psh_dynamic_pending_take(PshState *out){
 if(!dynamic_pending)return false;dynamic_pending--;
 memset(out,0,sizeof(*out));out->combiner_control=0xdeadu;return true;
}
static void spec_track_adopted(PGRAPHGLState *r,const PshState *state){
 (void)r;CHECK(state->combiner_control>=32&&state->combiner_control<36);notified++;
}
/* TAKE */
static bool comb_async_take(PGRAPHGLState *r,PshState *out,bool *gate){
 CHECK(++take_calls<=32);return production_comb_async_take(r,out,gate);
}
static bool service(PGRAPHGLState *r){
/* FEED */
 return fed;
}
static void make_state(ShaderState *state,unsigned id,bool geometry){
 memset(state,0,sizeof(*state));state->vsh.uniform_attrs=1u<<id;
 state->vsh.programmable.program_length=1;
 state->vsh.programmable.program_data[0][0]=0x64103u+id;
 state->geom.primitive_mode=geometry?PRIM_TYPE_TRIANGLES:PRIM_TYPE_POINTS;
 state->geom.polygon_front_mode=state->geom.polygon_back_mode=POLY_MODE_FILL;
 state->geom.tri_rot0=id;
 state->psh.combiner_control=32+id;
 state->psh.depth_format=DEPTH_FORMAT_UI_CLAMP;
 state->psh.shader_stage_program=PS_TEXTUREMODES_PROJECT2D<<(5*(id&3));
}
static void run(unsigned occupied,unsigned cache_mask,bool geometry,unsigned count,unsigned start,bool dynamic,bool throttled){
 PGRAPHGLState r={0};ShaderModuleCacheKey expected[2][3];unsigned stages[2];
 r.spec_pending_r=start;r.spec_pending_w=start+count;
 memset(gen_jobs,0,sizeof(gen_jobs));memset(comb_async,0,sizeof(comb_async));
 cached_n=published=notified=take_calls=0;dynamic_pending=dynamic;
 memset(cached,0,sizeof(cached));for(unsigned bin=0;bin<4;bin++)QTAILQ_INIT(&r.shader_module_cache.bins[bin]);
 if(throttled)for(unsigned i=0;i<12;i++)comb_async[i].used=true;
 for(unsigned n=0;n<count;n++){
  ShaderState state;make_state(&state,n,geometry);r.spec_pending[(start+n)%4]=state;
  bool need=state_module_keys(&state,expected[n]);CHECK(need==geometry);stages[n]=need?0:1;
  CHECK(expected[n][1].vsh.glsl_opts.prefix_outputs==geometry);
  if(geometry)CHECK(expected[n][0].geom.glsl_opts.ui_clamp_stage==n+1);
  for(unsigned stage=stages[n];stage<3;stage++)if(cache_mask&(1u<<stage)){
   ShaderModuleCacheEntry *entry=&cached[cached_n++];entry->key=expected[n][stage];
   entry->node.hash=xxh3_64(&entry->key,sizeof(entry->key), 0);
   unsigned bin=lru_hash_to_bin(&r.shader_module_cache,entry->node.hash);
   QTAILQ_INSERT_TAIL(&r.shader_module_cache.bins[bin],&entry->node,next_bin);
  }
 }
 for(unsigned n=0;n<count;n++){
  const ShaderState *state=&r.spec_pending[(start+n)%4];
  unsigned required=geometry?7:6;
  bool complete=(cache_mask&required)==required;
  CHECK(state_modules_known(&r,state)==complete);
  for(int phase=0;phase<=SPEC_PIPE_RESERVED;phase++){
   r.spec_track[0].phase=phase;
   CHECK(draw_ready_t(&r,-1,*state)==false);
   CHECK(draw_ready_ts(&r,-1,*state)==false);
   CHECK(draw_ready_t(&r,0,*state)==(complete&&(phase==SPEC_ADOPTED||phase==SPEC_PIPE_RESERVED)));
   CHECK(draw_ready_ts(&r,0,*state)==(complete&&phase==SPEC_ADOPTED));
  }
 }
 unsigned free_slots=0;
 for(unsigned i=0;i<GEN_JOBS;i++){
  if(occupied&(1u<<i)){
   gen_jobs[i].used=true;gen_jobs[i].key.kind=0x100u+i;
   gen_jobs[i].gate=true;gen_jobs[i].seed=true;
   gen_jobs[i].code=(MString *)(uintptr_t)(i+1);
   gen_jobs[i].state=(i&1)?GEN_COMPLETE:GEN_QUEUED;
  }else free_slots++;
 }
 unsigned dynamic_done=dynamic&&free_slots;
 unsigned remaining=free_slots-dynamic_done,consumed=0,needed=dynamic_done;
 if(!throttled)for(unsigned n=0;n<count&&remaining>=3;n++){
  unsigned missing=0;for(unsigned stage=stages[n];stage<3;stage++)missing+=!(cache_mask&(1u<<stage));
  CHECK(missing<=remaining);remaining-=missing;needed+=missing;consumed++;
 }
 bool fed=service(&r);
 CHECK(fed==(needed!=0));CHECK(published==needed);
 CHECK(r.spec_pending_r==start+consumed&&r.spec_pending_w==start+count);
 CHECK(dynamic_pending==(unsigned)dynamic-dynamic_done);
 CHECK(notified==((cache_mask&4)?consumed:0));
 /* A consumed source may be overwritten immediately. Pending records must
  * remain intact, and every submitted key must survive that overwrite. */
 for(unsigned n=0;n<consumed;n++)memset(&r.spec_pending[(start+n)%4],0xa5,sizeof(ShaderState));
 for(unsigned n=consumed;n<count;n++){ShaderState state;make_state(&state,n,geometry);CHECK(!memcmp(&r.spec_pending[(start+n)%4],&state,sizeof(state)));}
 for(unsigned n=0;n<consumed;n++)for(unsigned stage=stages[n];stage<3;stage++){
  unsigned matches=0;for(unsigned i=0;i<GEN_JOBS;i++)if(gen_jobs[i].used&&!memcmp(&gen_jobs[i].key,&expected[n][stage],sizeof(expected[n][stage]))){
   CHECK(gen_jobs[i].state==GEN_QUEUED&&!gen_jobs[i].gate&&!gen_jobs[i].seed&&!gen_jobs[i].code);matches++;
  }
  CHECK(matches==((cache_mask&(1u<<stage))?0u:1u));
 }
 for(unsigned i=0;i<GEN_JOBS;i++)if(occupied&(1u<<i)){
  CHECK(gen_jobs[i].used&&gen_jobs[i].key.kind==0x100u+i&&gen_jobs[i].gate&&gen_jobs[i].seed);
  CHECK(gen_jobs[i].state==((i&1)?GEN_COMPLETE:GEN_QUEUED));CHECK(gen_jobs[i].code==(MString *)(uintptr_t)(i+1));
 }
 /* Retire only this fixture's occupied owners and retry the held record. */
 for(unsigned i=0;i<GEN_JOBS;i++){gen_jobs[i].used=false;gen_jobs[i].code=NULL;gen_jobs[i].state=GEN_IDLE;}
 memset(comb_async,0,sizeof(comb_async));published=take_calls=0;
 service(&r);CHECK(r.spec_pending_r==start+count&&dynamic_pending==0);
}
int main(void){
 unsigned cases=0;
 for(unsigned mask=0;mask<(1u<<GEN_JOBS);mask++)for(unsigned cache=0;cache<8;cache++)for(unsigned geometry=0;geometry<2;geometry++){
  run(mask,cache,geometry,1,0,false,false);cases++;
 }
 for(unsigned free=0;free<=GEN_JOBS;free++)for(unsigned cache=0;cache<8;cache++)for(unsigned geometry=0;geometry<2;geometry++)for(unsigned dynamic=0;dynamic<2;dynamic++)for(unsigned throttle=0;throttle<2;throttle++){
  unsigned occupied=free==16?0:0xffffu<<free&0xffffu;
  run(occupied,cache,geometry,2,UINT_MAX-1,dynamic,throttle);cases++;
 }
 printf("GL specialization preparation: %u pressure/cache/topology/wrap/priority/retry/lifetime cases PASS\n",cases);
 printf("Existing owned module key: %zu bytes; at most three keys per full request, one containing vertex microcode\n",sizeof(ShaderModuleCacheKey));
}
'''.replace('/* KEY */',key).replace('/* RECORDS */',records).replace('/* JOB FUNCTIONS */',
    (function(source,'static bool gen_job_push(')+'\n'+function(source,'static bool gen_jobs_have_free(unsigned needed)\n{')).replace(
        'retro_atomic_store_release_int(', 'fixture_publish(')).replace(
    '/* GEOM */',function(geom,'bool pgraph_glsl_need_geom(')).replace('/* KEYS */',function(source,'static bool state_module_keys(')).replace(
    '/* KNOWN */',function(source,'static bool module_known(')+'\n'+function(source,'static bool state_modules_known(')).replace(
    '/* TAKE */',function(source,'static bool comb_async_take(').replace(
        'static bool comb_async_take(', 'static bool production_comb_async_take(', 1)).replace('/* FEED */',feed)

code=code.replace('/* DRAW CONDITIONS */','\n'.join(draw_conditions))

with tempfile.TemporaryDirectory(prefix='gl-spec-preparation-') as temporary:
    folder=Path(temporary);(folder/'qemu').mkdir()
    (folder/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
    (folder/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
    c=folder/'test.c';binary=folder/('test.exe' if os.name=='nt' else 'test')
    command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter',
             '-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(folder),'-I'+str(root),
             '-I'+str(root/'libretro-common/include'),str(c),'-o',str(binary)]
    c.write_text(code);subprocess.run(command,check=True);subprocess.run([str(binary)],check=True,timeout=120)
    mutants=[('specialization draw guard removed',code.replace('&&\n            state_modules_known(r, &state)','',1)),('interpreter draw guard removed',code.replace('&&\n                        state_modules_known(r, &state)','',1)),('fragment-only readiness',code.replace('return (!need_gs || module_known(r, &keys[0])) &&\n           module_known(r, &keys[1]) && module_known(r, &keys[2]);','(void)need_gs; return module_known(r, &keys[2]);',1)),('one-slot dequeue loses stages',code.replace('!gen_jobs_have_free(3)','!gen_jobs_have_free(1)',1)),
             ('retire before stage ownership',code.replace('        *gate = false;', '        r->spec_pending_r++;\n        *gate = false;',1)),
             ('omit geometry preparation',code.replace('unsigned stage = need_gs ? 0 : 1','unsigned stage = 1',1).replace('bool need_gs = state_module_keys(queued, keys);','(void)state_module_keys(queued, keys);',1)),
             ('fragment-only preparation',code.replace('unsigned stage = need_gs ? 0 : 1','unsigned stage = 2',1).replace('bool need_gs = state_module_keys(queued, keys);','(void)state_module_keys(queued, keys);',1)),
             ('hash-only cache identity',code.replace('node->hash == hash && !memcmp(&module->key, key, sizeof(*key))','node->hash == hash',1).replace('        ShaderModuleCacheEntry *module =\n            container_of(node, ShaderModuleCacheEntry, node);','',1))]
    for name,mutant in mutants:
        assert mutant!=code,name
        c.write_text(mutant);subprocess.run(command,check=True)
        bad=subprocess.run([str(binary)],capture_output=True,timeout=120)
        assert bad.returncode==1 and b'FAIL GL specialization preparation' in bad.stderr,name
        print('Required negative control rejected:',name)

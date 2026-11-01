#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify production speculative keys are built and published in their final slot."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
fn=re.search(r'^static void pipeline_ahead\(.*?^\}',s,re.M|re.S).group()
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
key=re.search(r'typedef struct PipelineKey \{.*?\} PipelineKey;',h,re.S).group()
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
typedef struct { uint8_t bytes[128]; } ShaderState;
typedef struct { uint32_t words[8]; } RenderPassState;
/* KEY */
typedef struct { unsigned id; } ShaderModuleInfo;
typedef struct {
    PipelineKey key;uint64_t hash,state_hash;ShaderModuleInfo *mods[3];int nmods;
} LibJob;
typedef struct {
    bool ahead_valid;ShaderState ahead_state;uint64_t ahead_hash;unsigned pipeline_cache;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static LibJob slot;
static PipelineKey expected;
static bool ring_full,cache_hit,in_flight,with_geom;
static unsigned drained,filled,submitted,landed,queued,refs;
static ShaderModuleInfo modules[3];
static uint64_t hash_bytes(const uint8_t *p,size_t n) {
    uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<n;i++) h=(h^p[i])*1099511628211ull;
    return h;
}
static uint64_t fast_hash(const uint8_t *p,size_t n) {
    assert(p==(const uint8_t *)&slot.key && n==sizeof(slot.key));
    return hash_bytes(p,n);
}
static void pump_drain(PGRAPHState *pg) {(void)pg;drained++;}
static LibJob *pump_slot(void) {if(ring_full)return NULL;memset(&slot,0,sizeof(slot));return &slot;}
static bool lru_contains_hash(unsigned *cache,uint64_t hash) {(void)cache;assert(hash==hash_bytes((const uint8_t *)&expected,sizeof(expected)));return cache_hit;}
static bool pump_full_in_flight(uint64_t hash) {assert(hash==hash_bytes((const uint8_t *)&expected,sizeof(expected)));return in_flight;}
static void pgraph_vk_spec_pipeline_landed(uint64_t hash) {assert(hash==123);landed++;}
static void pgraph_vk_spec_pipeline_queued(uint64_t hash) {assert(hash==123);queued++;}
static void pgraph_vk_ref_state_modules(PGRAPHVkState *r,const ShaderState *state,
 ShaderModuleInfo **geom,ShaderModuleInfo **vsh,ShaderModuleInfo **psh) {
 assert(state==&r->ahead_state);refs++;*vsh=&modules[0];*geom=with_geom?&modules[1]:NULL;*psh=&modules[2];
}
static void pipeline_job_fill(PGRAPHState *pg,const ShaderState *state,
 const ShaderModuleInfo *vsh,const ShaderModuleInfo *geom,const ShaderModuleInfo *psh,
 const PipelineKey *key,LibJob *job) {
 (void)pg;(void)state;assert(vsh==&modules[0] && geom==(with_geom?&modules[1]:NULL) && psh==&modules[2]);
 assert(job==&slot && key==&slot.key && !memcmp(key,&expected,sizeof(*key)));filled++;
}
static void pump_submit(LibJob *job) {
 assert(job==&slot && !memcmp(&job->key,&expected,sizeof(expected)));
 assert(job->hash==hash_bytes((const uint8_t *)&expected,sizeof(expected)) && job->state_hash==123);
 assert(job->nmods==(with_geom?3:2) && job->mods[0]==&modules[0] && job->mods[job->nmods-1]==&modules[2]);
 submitted++;
}
/* PRODUCTION */
int main(void) {
 unsigned cases=0;
 for(unsigned serial=0;serial<128;serial++)for(unsigned valid=0;valid<2;valid++)
 for(unsigned full=0;full<2;full++)for(unsigned hit=0;hit<2;hit++)
 for(unsigned flight=0;flight<2;flight++)for(unsigned geom=0;geom<2;geom++) {
  PGRAPHVkState r={.ahead_valid=valid,.ahead_hash=123};PGRAPHState pg={&r};
  PipelineKey draw;memset(&draw,serial,sizeof(draw));memset(&r.ahead_state,serial^0xa5,sizeof(r.ahead_state));
  memcpy(&expected,&draw,sizeof(expected));memcpy(&expected.shader_state,&r.ahead_state,sizeof(r.ahead_state));
  ring_full=full;cache_hit=hit;in_flight=flight;with_geom=geom;
  drained=filled=submitted=landed=queued=refs=0;
  pipeline_ahead(&pg,&draw);
  assert(!r.ahead_valid && drained==valid);
  bool success=valid && !full && !hit && !flight;
  assert(filled==success && submitted==success && queued==success && refs==success);
  assert(landed==(valid && !full && (hit || flight)));
  /* A queued key cannot retain caller or mutable renderer storage. */
  memset(&draw,0xdd,sizeof(draw));memset(&r,0xee,sizeof(r));
  if(success)assert(!memcmp(&slot.key,&expected,sizeof(expected)));
  cases++;
 }
 printf("Pipeline ahead keys: %u in-place/hash/metadata/module-order/cached/in-flight/backpressure/caller-lifetime cases PASS\n",cases);
}
'''.replace('/* KEY */',key).replace('/* PRODUCTION */',fn)
with tempfile.TemporaryDirectory(prefix='pipeline-ahead-key-') as tmp:
 d=Path(tmp);c=d/'test.c';c.write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(r/'deps/vulkan-headers'),*shlex.split(os.environ.get('PIPELINE_AHEAD_CFLAGS','')),str(c),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)

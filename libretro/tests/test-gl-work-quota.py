# SPDX-License-Identifier: GPL-2.0-or-later
"""Production seed work quotas retain fairness behind unavailable modules."""
from pathlib import Path
import subprocess, sys, tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(root/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
a=s.index('static void seed_pipe_pump(');b=s.index('\nstatic void comb_async_service',a)
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
typedef struct {unsigned id;} ShaderState;
typedef struct {unsigned id;} ShaderModuleCacheKey;
typedef struct {unsigned id;} LruNode;
typedef struct {LruNode node;bool initialized;unsigned gl_pipeline;} ShaderBinding;
typedef struct {
 struct {int state;unsigned pipe_n,pipe_cursor,mod_idx,mod_n,pipes_built,stall_flips;ShaderState *pipe_states;} seed;
 ShaderBinding *shader_binding;int shader_cache;
} PGRAPHGLState;
static bool ready[16],inflight;
static ShaderBinding bindings[16];
static unsigned visits,built,finished,restored;
static bool state_module_keys(ShaderState *s,ShaderModuleCacheKey keys[3]){visits++;for(int i=0;i<3;i++)keys[i].id=s->id;return false;}
static bool module_known(PGRAPHGLState*r,ShaderModuleCacheKey*k){(void)r;return ready[k->id];}
static uint64_t xxh3_64(const uint8_t*p,size_t n, uint64_t seed __attribute__((unused))){assert(n==sizeof(ShaderState));return ((const ShaderState*)p)->id;}
static LruNode *lru_lookup(int *cache,uint64_t h,ShaderState*s){(void)cache;assert(h==s->id);return &bindings[h].node;}
static void generate_shaders(PGRAPHGLState*r,ShaderBinding*b){(void)r;assert(ready[b->node.id]&&!b->initialized);b->initialized=true;built++;}
static void glUseProgram(unsigned p){assert(!p);}
static void glBindProgramPipeline(unsigned p){assert(p==42);restored++;}
static bool seed_jobs_in_flight(PGRAPHGLState*r){(void)r;return inflight;}
static void seed_finish(PGRAPHGLState*r,const char*reason){assert(reason);r->seed.state=0;finished++;}
/* PRODUCTION */
int main(void){
 ShaderState states[16];ShaderBinding live={.initialized=true,.gl_pipeline=42};
 for(unsigned i=0;i<16;i++){states[i].id=i;bindings[i].node.id=i;ready[i]=i>=10;}
 PGRAPHGLState r={.seed={.state=1,.pipe_n=16,.pipe_states=states},.shader_binding=&live};
 inflight=true;
 for(unsigned call=0;call<16;call++){visits=0;seed_pipe_pump(&r,4);assert(visits<=4);}
 assert(built==6 && r.seed.pipe_n==10 && restored && !finished);
 for(unsigned i=0;i<16;i++)ready[i]=true;
 for(unsigned call=0;call<16&&r.seed.state;call++){visits=0;seed_pipe_pump(&r,4);assert(visits<=4);}
 assert(built==16 && r.seed.pipe_n==0 && finished==1);
 /* A zero quota performs no work; unavailable prefixes cannot hide later entries. */
 r.seed.state=1;r.seed.pipe_n=16;r.seed.pipe_cursor=r.seed.stall_flips=0;
 for(unsigned i=0;i<16;i++){states[i].id=i;ready[i]=false;}
 inflight=false;visits=0;seed_pipe_pump(&r,0);assert(!visits && r.seed.pipe_n==16);
 for(unsigned call=0;call<8000&&r.seed.state;call++){visits=0;seed_pipe_pump(&r,4);assert(visits<=4);}
 assert(!r.seed.state && finished==2);
 puts("GL work quotas: bounded work, blocked-prefix fairness, completion and stale-seed retirement PASS");
}
"""
code=code.replace('/* PRODUCTION */',s[a:b])
with tempfile.TemporaryDirectory(prefix='gl-work-quota-') as t:
 t=Path(t);(t/'test.c').write_text(code);binary=t/'test.exe'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(t/'test.c'),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True,timeout=30)

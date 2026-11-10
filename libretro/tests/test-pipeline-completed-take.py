#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Completed queue handles transfer once; in-flight owners remain untouched."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(name):
 m=re.search(r'^(?:static )?(?:bool|void|size_t|VkPipeline) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
parts='\n'.join(fn(n) for n in ('gpl_key_size','pump_take_library','pump_take_pipeline','library_land','pump_drain'))
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ('RenderPassState','PipelineKey','PipelineBinding'))
types+='\n'+'\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',s,re.S)[0] for n in ('GplPreKey','GplFragKey'))
job=re.search(r'typedef struct \{\n    bool frag;.*?\} LibJob;',s,re.S)[0]
if 'S3tcPipelineJob' in job:job=re.search(r'typedef struct S3tcPipelineJob \{.*?\} S3tcPipelineJob;',(r/'hw/xbox/nv2a/pgraph/vk/s3tc-pipeline-job.h').read_text(),re.S)[0]+'\n'+job
types+='\n'+job
if 's3tc_pipeline_job_land(' in parts:parts='static void s3tc_pipeline_job_land(PGRAPHVkState *r,LibJob *job){(void)r;(void)job;CHECK(false); }\n'+parts
for name in ('pump_take_library','pump_take_pipeline'):
 for banned in ('vkCreate','Wait','memcpy(','g_new','malloc','realloc','pump_d++','pump_drain('):assert banned not in fn(name),(name,banned)
for name in ('gpl_get_pre','gpl_get_frag'):
 body=fn(name);assert body.index('gpl_lookup(')<body.index('pump_take_library(')<body.index('vkCreateGraphicsPipelines(')
body=fn('create_pipeline');assert body.index('lru_lookup(')<body.index('pump_take_pipeline(')<body.index('pipeline_use_ready_sibling(')<body.index('vkCreateGraphicsPipelines(')
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL completed take line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define PUMP_RING 64
typedef struct{unsigned unused;}LruNode;
typedef struct{unsigned refs;}ShaderModuleInfo;
/* TYPES */
typedef struct{bool dyn_vertex_input;VkDevice device;}PGRAPHVkState;
struct PGRAPHState{PGRAPHVkState *vk_renderer_state;unsigned draw_time;};
static LibJob pump_jobs[PUMP_RING];static unsigned pump_d,pump_w_local,pump_r;
static bool pipe_started;static unsigned stores,lands,releases,markers,queries;static uint64_t marker;
static unsigned retro_atomic_load_acquire_int(unsigned *p){CHECK(p==&pump_r);queries++;return *p;}
static void gpl_store(PGRAPHVkState *r,bool frag,uint64_t hash,const void *key,VkPipeline lib){(void)r;(void)frag;(void)hash;CHECK(key&&lib);stores++;}
static VkPipeline gpl_lookup(PGRAPHVkState *r,bool frag,uint64_t hash,const void *key,bool *found){(void)r;(void)frag;(void)hash;CHECK(key);*found=false;return VK_NULL_HANDLE;}
static void vkDestroyPipeline(VkDevice device,VkPipeline pipeline,const void *alloc){(void)device;(void)pipeline;(void)alloc;CHECK(false);}
static void pipeline_land(PGRAPHState *pg,LibJob *job){CHECK(pg&&job->result);lands++;}
static void pgraph_vk_unref_shader_module(PGRAPHVkState *r,ShaderModuleInfo *m){CHECK(r&&m->refs);m->refs--;releases++;}
static void pgraph_vk_spec_pipeline_landed(uint64_t hash){marker=hash;markers++;}
/* PRODUCTION */
int main(void){unsigned cases=0;uint32_t random=13;
 for(unsigned trial=0;trial<64;trial++)for(unsigned kind=0;kind<3;kind++)for(unsigned mode=0;mode<12;mode++)for(unsigned dynamic=0;dynamic<2;dynamic++){
  memset(pump_jobs,0,sizeof(pump_jobs));PGRAPHVkState r={.dyn_vertex_input=dynamic};PGRAPHState pg={.vk_renderer_state=&r,.draw_time=771};
  unsigned prefix=trial&8?63:trial%4;pump_d=trial&1?UINT32_MAX-trial/2:trial;pump_w_local=pump_d+prefix+1;pump_r=pump_d+prefix+(mode!=1);pipe_started=mode!=2;
  LibJob *job=&pump_jobs[(pump_d+prefix)%PUMP_RING];job->full=kind==2;job->frag=kind==1;
  unsigned char *bytes=(unsigned char *)&job->key;for(unsigned i=0;i<sizeof(job->key);i++){random=random*1664525u+1013904223u;bytes[i]=random>>24;}
  job->hash=779;job->state_hash=992;job->result=(VkPipeline)(uintptr_t)(mode==3?0:123);job->layout=(VkPipelineLayout)(uintptr_t)311;job->ci.renderPass=(VkRenderPass)(uintptr_t)455;job->has_dynamic_line_width=true;
  ShaderModuleInfo owner={.refs=1};job->mods[0]=&owner;job->nmods=1;
  PipelineKey key;memcpy(&key,&job->key,sizeof(key));GplPreKey pre;memcpy(&pre,&job->pre_key,sizeof(pre));GplFragKey frag;memcpy(&frag,&job->frag_key,sizeof(frag));
  void *query=kind==2?(void *)&key:kind==1?(void *)&frag:(void *)&pre;
  size_t size=kind==2?sizeof(key):gpl_key_size(&r,kind==1);
  if(mode==4)((unsigned char *)query)[0]^=1;if(mode==5)((unsigned char *)query)[size-1]^=1;
  uint64_t hash=job->hash+(mode==6);
  if(mode==7)job->full=!job->full;if(mode==8&&kind!=2)job->frag=!job->frag;
  if(mode==9){pump_r=pump_d+prefix;/* a tempting identical but unpublished result */}
  PipelineBinding binding={0},before;memcpy(&binding.key,&key,sizeof(key));memcpy(&before,&binding,sizeof(binding));
  stores=lands=releases=markers=queries=0;marker=0;unsigned start=pump_d;
  bool expected=pipe_started&&pump_r==pump_w_local&&job->result&&mode!=4&&mode!=5&&mode!=6&&mode!=7&&!(mode==8&&kind!=2);
  bool taken=kind==2?pump_take_pipeline(&pg,&binding,hash,&key):pump_take_library(&r,kind==1,hash,query)!=VK_NULL_HANDLE;
  CHECK(taken==expected&&pump_d==start&&owner.refs==1&&releases==0);
  CHECK(stores==(expected&&kind!=2)&&markers==(expected&&kind==2));
  if(expected){CHECK(!job->result);if(kind==2){CHECK(binding.pipeline==(VkPipeline)(uintptr_t)123&&binding.layout==job->layout&&binding.render_pass==job->ci.renderPass&&binding.has_dynamic_line_width&&binding.draw_time==771&&marker==992);}
   CHECK(!(kind==2?pump_take_pipeline(&pg,&binding,hash,&key):pump_take_library(&r,kind==1,hash,query)!=VK_NULL_HANDLE));
  }else if(kind==2)CHECK(!memcmp(&binding,&before,sizeof(binding)));
  /* Poison the query: neither native owner nor the queue retains it. */
  memset(query,0xa5,size);pipe_started=true;pump_r=pump_w_local;pump_drain(&pg);
  CHECK(pump_d==start+prefix+1&&owner.refs==0&&releases==1);
  CHECK(stores+lands+(expected&&kind==2)==(unsigned)(mode!=3));CHECK(!memcmp(&binding.key,&before.key,sizeof(key)));cases++;
 }
 printf("Completed pipeline take: %u native-key/publication/type/collision/owner/drain/wrap cases PASS\n",cases);
}
'''.replace('/* TYPES */',types).replace('/* PRODUCTION */',parts)
if 'S3tcPipelineJob' in job:
 extra=r"""
 for(unsigned full=0;full<2;full++)for(unsigned frag=0;frag<2;frag++){
  memset(pump_jobs,0,sizeof(pump_jobs));LibJob *j=&pump_jobs[0];j->compute=true;j->full=full;j->frag=frag;j->hash=9;j->result=(VkPipeline)(uintptr_t)55;
  pump_d=0;pump_w_local=pump_r=1;pipe_started=true;PGRAPHVkState r={0};PGRAPHState pg={.vk_renderer_state=&r};PipelineBinding b={0};stores=markers=0;
  const void *query=frag?(const void*)&j->frag_key:(const void*)&j->pre_key;
  CHECK(!pump_take_library(&r,frag,9,query));CHECK(!pump_take_pipeline(&pg,&b,9,&j->key));
  CHECK(j->result==(VkPipeline)(uintptr_t)55&&!b.pipeline&&!stores&&!markers&&pump_d==0);cases++;
 }
"""
 code=code.replace(' printf("Completed pipeline take:',extra+' printf("Completed pipeline take:')
with tempfile.TemporaryDirectory(prefix='pipeline-completed-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),str(c),'-o',str(exe)]
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 c.write_text(code);subprocess.run(cmd,check=True,env=env);subprocess.run([str(exe)],check=True,env=env)
 for name,mutant in [('wrapped result lost',code.replace('i != done','i < done',1)),('unfinished result',code.replace('retro_atomic_load_acquire_int(&pump_r)','pump_w_local',1)),('double ownership',code.replace('job->result = VK_NULL_HANDLE;','/* stale handle retained */',1)),('library hash-only match',code.replace('!memcmp(owned, key, gpl_key_size(r, frag))','((void)owned,(void)key,true)',1)),('pipeline hash-only match',code.replace('!memcmp(&job->key, key, sizeof(*key))','((void)key,true)',1))]:
  assert mutant!=code,name;c.write_text(mutant);subprocess.run(cmd,check=True,env=env)
  assert subprocess.run([str(exe)],env=env,capture_output=True).returncode!=0,name
 if 'S3tcPipelineJob' in job:
  for name,mutant in [('compute mistaken for library',code.replace('!job->compute && !job->full','!job->full',1)),('compute mistaken for whole graphics pipeline',code.replace('!job->compute && job->full && job->result','job->full && job->result',1))]:
   assert mutant!=code,name;c.write_text(mutant);subprocess.run(cmd,check=True,env=env)
   assert subprocess.run([str(exe)],env=env,capture_output=True).returncode!=0,name
   print('Required compute-family control rejected:',name)
 print('Completed pipeline take mutations rejected')

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The optional seed owner and one-shot flag end with their renderer."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text()
service=re.search(r'^void pgraph_vk_seed_service\(.*?^\}',s,re.M|re.S).group()
finalize=re.search(r'^void pgraph_vk_finalize_shaders\(.*?^\}',s,re.M|re.S).group()
renderer=(r/'hw/xbox/nv2a/pgraph/vk/renderer.c').read_text()
shutdown=re.search(r'^static void pgraph_vk_finalize\(.*?^\}',renderer,re.M|re.S).group()
assert shutdown.index('pgraph_vk_finalize_pipelines(pg)') < shutdown.index('pgraph_vk_finalize_shaders(pg)')
draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
pipe_cleanup=re.search(r'^void pgraph_vk_finalize_pipelines\(.*?^\}',draw,re.M|re.S).group()
assert pipe_cleanup.index('pump_quiesce(pg)') < pipe_cleanup.index('g_free(r->pump.state_hash)') < pipe_cleanup.index('memset(&r->pump, 0, sizeof(r->pump))')
code=r'''
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define TRUE true
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL seed lifetime line=%d\n",__LINE__);_Exit(1);}}while(0)
typedef struct {unsigned generation;} GArray;
typedef struct {unsigned seed_writeback_complete;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static struct {struct {bool shader_seeding;} perf;} g_config;
static bool vk_seed_done,tag,quiesced,pipeline_quiesced;
static GArray *vk_pump_states,bank;
static unsigned initialized,steps,freed;
static const char *pgraph_seed_game_tag(void){CHECK(g_config.perf.shader_seeding);return tag?"fixture":NULL;}
static void vk_seed_siblings(void){CHECK(!vk_pump_states);initialized++;bank.generation=initialized;vk_pump_states=&bank;}
static void vk_seed_step(void){CHECK(g_config.perf.shader_seeding);steps++;}
static void vk_seed_write(PGRAPHState *pg){(void)pg;}
static void retro_eventcount_free(unsigned *p){(void)p;}
static void shader_cache_finalize(PGRAPHState *pg){(void)pg;CHECK(pipeline_quiesced);}
static void destroy_descriptor_sets(PGRAPHState *pg){(void)pg;}
static void destroy_descriptor_set_layout(PGRAPHState *pg){(void)pg;}
static void destroy_descriptor_pool(PGRAPHState *pg){(void)pg;}
static void vk_spec_quiesce(void){quiesced=true;}
static void g_array_free(GArray *p,bool all){CHECK(quiesced&&pipeline_quiesced&&p==&bank&&all);freed++;}
/* PRODUCTION */
int main(void){
 PGRAPHVkState r={0};PGRAPHState pg={&r};
 for(unsigned generation=0;generation<3;generation++){
  CHECK(!vk_pump_states&&!vk_seed_done);
  quiesced=pipeline_quiesced=false;g_config.perf.shader_seeding=false;tag=true;
  unsigned old_steps=steps;
  pgraph_vk_seed_service();CHECK(initialized==generation&&steps==old_steps&&!vk_seed_done);
  g_config.perf.shader_seeding=true;tag=false;
  pgraph_vk_seed_service();CHECK(initialized==generation&&!vk_seed_done);
  tag=true;pgraph_vk_seed_service();pgraph_vk_seed_service();
  CHECK(initialized==generation+1&&vk_seed_done&&vk_pump_states==&bank);
  g_config.perf.shader_seeding=false;old_steps=steps;
  pgraph_vk_seed_service();CHECK(steps==old_steps&&initialized==generation+1);
  pipeline_quiesced=true;pgraph_vk_finalize_shaders(&pg);
  CHECK(!vk_pump_states&&!vk_seed_done&&freed==generation+1);
 }
 puts("Seed lifetime: Off/late-title/enable/disable and three renderer generations; no stale bank, early free or repeated initialization PASS");
}
'''.replace('/* PRODUCTION */',service+'\n'+finalize)
with tempfile.TemporaryDirectory(prefix='seed-lifetime-') as folder:
 p=Path(folder);c=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 command=[cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(c),'-o',str(exe)]
 c.write_text(code);subprocess.run(command,check=True);subprocess.run([str(exe)],check=True)
 for label,old,new in [
  ('stale one-shot flag','vk_seed_done = false;','/* missing flag reset */'),
  ('bank released before worker quiescence','    vk_spec_quiesce();','    if(vk_pump_states){g_array_free(vk_pump_states,TRUE);vk_pump_states=NULL;}\n    vk_spec_quiesce();')]:
  assert old in code
  c.write_text(code.replace(old,new,1));subprocess.run(command,check=True)
  bad=subprocess.run([str(exe)],capture_output=True)
  assert bad.returncode==1 and b'FAIL seed lifetime' in bad.stderr,label
  print('Required negative control rejected:',label)

# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise production buffer growth policy with allocation/lifetime spies."""
from pathlib import Path
import os, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(r/'hw/xbox/nv2a/pgraph/vk/buffer.c').read_text()
a=s.index('void pgraph_vk_buffer_ensure_capacity(')
b=s.index('\nVkDeviceSize pgraph_vk_append_to_buffer',a)
code=r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t VkDeviceSize;
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ROUND_UP(a,b) (((a)+(b)-1)&~((b)-1))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
enum { BUFFER_STAGING_SRC, BUFFER_STAGING_DST, BUFFER_COMPUTE_SRC,
       BUFFER_COMPUTE_DST, STAGED, DEVICE, OTHER, NBUF };
enum { VK_FINISH_REASON_NEED_BUFFER_SPACE };
typedef struct { VkDeviceSize buffer_size; unsigned handle; } StorageBuffer;
typedef struct { int staging, device; } StagedBuffer;
static const StagedBuffer pgraph_vk_staged_buffers[]={{STAGED,DEVICE}};
typedef struct { StorageBuffer storage_buffers[NBUF],alt_staging[1],alt_device[1];
                 int in_command_buffer; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; } PGRAPHState;
static unsigned step, finished, waited, settled, recreated, changed, generation;
static void pgraph_vk_finish(PGRAPHState *pg,int reason)
{ (void)reason; assert(!step++); finished++; pg->vk_renderer_state->in_command_buffer=0; }
static void pgraph_vk_wait_for_aux(PGRAPHState *pg)
{ (void)pg; assert(step<=1); step=2; waited++; }
static void pgraph_vk_settle_all_slots(PGRAPHState *pg)
{ (void)pg; assert(step==2); step=3; settled++; }
static void recreate_buffer(PGRAPHState *pg,StorageBuffer *b,VkDeviceSize size)
{ (void)pg; assert(step==3); b->buffer_size=size; b->handle=++generation; recreated++; }
static void pgraph_vk_compute_buffers_changed(PGRAPHState *pg)
{ (void)pg; assert(recreated==2); changed++; }
/* PRODUCTION */
int main(void) {
 const VkDeviceSize mb=1024*1024;
 for(unsigned open=0;open<2;open++) for(int index=0;index<NBUF;index++) {
  PGRAPHVkState r={0}; PGRAPHState pg={&r};
  for(int i=0;i<NBUF;i++) r.storage_buffers[i]=(StorageBuffer){64*mb,100+i};
  r.alt_staging[0]=(StorageBuffer){64*mb,200};
  r.alt_device[0]=(StorageBuffer){64*mb,201};
  r.in_command_buffer=open;
  step=finished=waited=settled=recreated=changed=0;
  pgraph_vk_buffer_ensure_capacity(&pg,index,64*mb);
  assert(!step && !recreated);
  pgraph_vk_buffer_ensure_capacity(&pg,index,65*mb);
  assert(finished==open && waited==1 && settled==1);
  for(int i=0;i<NBUF;i++) {
   int paired=(index==STAGED||index==DEVICE)?(i==STAGED||i==DEVICE):
    (index==BUFFER_COMPUTE_SRC||index==BUFFER_COMPUTE_DST)?
    (i==BUFFER_COMPUTE_SRC||i==BUFFER_COMPUTE_DST):i==index;
   assert(r.storage_buffers[i].buffer_size==(paired?128:64)*mb);
   assert(paired || r.storage_buffers[i].handle==(unsigned)(100+i));
  }
  assert(recreated==((index==STAGED||index==DEVICE)?4:
    (index==BUFFER_COMPUTE_SRC||index==BUFFER_COMPUTE_DST)?2:1));
  assert(changed==((index==BUFFER_COMPUTE_SRC||index==BUFFER_COMPUTE_DST)?1u:0u));
  if(index==STAGED||index==DEVICE) {
   assert(r.alt_staging[0].buffer_size==128*mb && r.alt_device[0].buffer_size==128*mb);
  } else assert(r.alt_staging[0].handle==200 && r.alt_device[0].handle==201);
  unsigned previous=recreated;
  pgraph_vk_buffer_ensure_capacity(&pg,index,128*mb);
  assert(recreated==previous);
 }
 puts("14 buffer-family growth cases passed: independent staging, paired compute/rotation, settle-before-recreate and no-op reuse");
}
'''.replace('/* PRODUCTION */',s[a:b])
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-buffer-growth-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare',str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)

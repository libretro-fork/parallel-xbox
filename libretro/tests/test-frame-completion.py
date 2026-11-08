# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production frame handoff against a waking GPU worker."""
from pathlib import Path
import subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve()
source=(repo/'ui/libretro.c').read_text(encoding='utf-8',errors='surrogateescape')
a=source.index('static void lr_finish_frame(void)')
b=source.index('/* One frame of guest time,',a)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',source[a:b])
request=(repo/'hw/xbox/nv2a/nv2a.c').read_text()
a=request.index('int nv2a_libretro_frame_end(void)');b=request.index('{',a)+1;depth=1
while depth:depth+=(request[b]=='{')-(request[b]=='}');b+=1
request=request[a:b]
request_test=r'''#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
typedef struct { struct { int thread_running, halt, frame_end_req; } pfifo; } NV2AState;
static NV2AState state, *g_nv2a;
static unsigned kicks;
static int retro_atomic_load_acquire_int(int *p) { return *p; }
static int retro_atomic_load_relaxed_int(int *p) { return *p; }
static void retro_atomic_store_release_int(int *p,int v) { *p=v; }
static void pfifo_kick(NV2AState *d) { assert(d==g_nv2a);kicks++; }
/* REQUEST */
int main(void) {
 assert(nv2a_libretro_frame_end()==0 && !kicks);
 g_nv2a=&state;
 assert(nv2a_libretro_frame_end()==0 && !kicks);
 state.pfifo.thread_running=1;state.pfifo.halt=1;
 assert(nv2a_libretro_frame_end()==0 && !kicks);
 state.pfifo.halt=0;
 int seeds[]={0,1,INT_MAX-1,INT_MAX};
 for(unsigned i=0;i<4;i++) {
  state.pfifo.frame_end_req=seeds[i];
  for(unsigned n=0;n<10000;n++) {
   int prev=state.pfifo.frame_end_req;
   int next=nv2a_libretro_frame_end();
   assert(next==(prev==INT_MAX?1:prev+1));
   assert(next==state.pfifo.frame_end_req);
  }
 }
 assert(kicks==40000);puts("PASS: 40000 bounded frame requests, wrap and stopped-owner checks");
}
'''.replace('/* REQUEST */',request)

with tempfile.TemporaryDirectory(prefix='frame-completion-') as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/'test.exe'
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 old='nv2a_libretro_frame_end();'
 assert old in code
 (p/'test.c').write_text(code.replace(old,old+' main_loop_wait(false);',1))
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 rejected=subprocess.run([str(exe)],capture_output=True)
 assert rejected.returncode==1 and b'frame handoff:' in rejected.stderr
 print('Required negative control rejected: synchronous frontend finish wait')
 (p/'test.c').write_text(request_test)
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

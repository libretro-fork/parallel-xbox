# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production optional timer handlers and fixed/adaptive load policy."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'system/cpu-timers.c').read_text();parts=[]
for name in ['get_timer_opt','put_timer_opt','warp_timer_post_load','icount_shift_pre_load','icount_shift_post_load']:
 m=re.search(r'^static int '+name+r'\(',s,re.M);assert m
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[m.start():b])
# Verify that all wire-compatible callbacks are actually selected by the
# subsection metadata, not merely defined beside the old pointer fields.
assert not re.search(r'VMSTATE_TIMER_PTR\(icount_(?:warp|rt|vm)_timer',s)
assert len(re.findall(r'VMSTATE_TIMER_PTR_OPT\(icount_(?:warp|rt|vm)_timer',s))==3
assert '.post_load = warp_timer_post_load' in s
assert '.pre_load = icount_shift_pre_load' in s and '.post_load = icount_shift_post_load' in s
macro=s[s.index('#define VMSTATE_TIMER_PTR_OPT'):].split('\n\n',1)[0]
assert '.flags = VMS_SINGLE' in macro and '.size = sizeof(QEMUTimer *)' in macro
assert '.info = &vmstate_info_timer_opt' in macro
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef struct {int64_t deadline;bool pending;unsigned mods,dels;} QEMUTimer;
typedef struct {uint8_t bytes[8];size_t pos,available;int error;} QEMUFile;
typedef struct {int unused;} VMStateField;
typedef struct {int unused;} JSONWriter;
typedef struct {QEMUTimer *icount_warp_timer;int64_t vm_clock_warp_start;int16_t icount_time_shift;int64_t last_delta,guard;} TimersState;
static int16_t icount_shift_configured;
enum {ICOUNT_ADAPTATIVE=2};static int mode;
static int icount_enabled(void){return mode;}
static uint64_t qemu_get_be64(QEMUFile*f){
 if(f->available<8){f->error=-EIO;f->pos=f->available;return 0;}
 uint64_t v=0;for(unsigned i=0;i<8;i++)v=(v<<8)|f->bytes[i];f->pos=8;return v;
}
static void timer_mod_ns(QEMUTimer*t,int64_t deadline){assert(t);t->deadline=deadline;t->pending=true;t->mods++;}
static void timer_del(QEMUTimer*t){assert(t);t->pending=false;t->dels++;}
static void timer_put(QEMUFile*f,QEMUTimer*t){
 assert(t);uint64_t v=t->pending?(uint64_t)t->deadline:UINT64_MAX;
 for(unsigned i=0;i<8;i++)f->bytes[i]=(uint8_t)(v>>((7-i)*8));f->pos=8;
}
/* PRODUCTION */
int main(void){
 const uint64_t deadlines[]={0,1,37,1000000,INT64_MAX,UINT64_C(0xfffffffffffffffe),UINT64_MAX};
 unsigned timers=0,shifts=0;
 for(unsigned own=0;own<2;own++)for(unsigned pending=0;pending<2;pending++)
 for(unsigned d=0;d<7;d++)for(unsigned available=0;available<=8;available++){
  QEMUTimer timer={.deadline=1234,.pending=pending};QEMUTimer *pointer=own?&timer:NULL;
  QEMUFile f={.available=available};for(unsigned i=0;i<8;i++)f.bytes[i]=deadlines[d]>>((7-i)*8);
  assert(get_timer_opt(&f,&pointer,sizeof(pointer),NULL)==0);
  assert(pointer==(own?&timer:NULL));
  assert(f.pos==available&&f.error==(available<8?-EIO:0));
  uint64_t expected=available<8?0:deadlines[d];
  assert(timer.mods==(own&&expected!=UINT64_MAX));
  assert(timer.dels==(own&&expected==UINT64_MAX));
  assert(timer.pending==(own?expected!=UINT64_MAX:pending));
  assert(timer.deadline==(own&&expected!=UINT64_MAX?(int64_t)expected:1234));
  if(own){
   QEMUFile output={0};QEMUTimer before=timer;
   assert(put_timer_opt(&output,&pointer,sizeof(pointer),NULL,NULL)==0);
   assert(output.pos==8&&!memcmp(&timer,&before,sizeof(timer)));
   uint64_t value=timer.pending?(uint64_t)timer.deadline:UINT64_MAX;
   for(unsigned i=0;i<8;i++)assert(output.bytes[i]==(uint8_t)(value>>((7-i)*8)));
  }
  TimersState state={.icount_warp_timer=pointer,.vm_clock_warp_start=987,.guard=0x12345678};
  assert(warp_timer_post_load(&state,1)==0);
  assert(state.vm_clock_warp_start==(own?987:-1)&&state.guard==0x12345678);
  timers++;
 }
 for(mode=0;mode<3;mode++)for(int configured=0;configured<64;configured++)
 for(int incoming=-1;incoming<64;incoming++){
  TimersState state={.icount_time_shift=configured,.last_delta=987,.guard=0x12345678};
  assert(icount_shift_pre_load(&state)==0);
  state.icount_time_shift=incoming;state.last_delta=654;
  assert(icount_shift_post_load(&state,2)==0);
  assert(state.icount_time_shift==(mode==ICOUNT_ADAPTATIVE?incoming:configured));
  assert(state.last_delta==654&&state.guard==0x12345678);shifts++;
 }
 printf("PASS: %u optional-timer and %u fixed/adaptive shift cases; wire bytes, absent owners, cancellation and warp reset\n",timers,shifts);
}
'''.replace('/* PRODUCTION */','\n'.join(parts))
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='icount-timer-state-') as d:
 p=Path(d);src=p/'test.c';exe=os.environ.get('IC_TIMER_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('IC_TIMER_CFLAGS','-fsanitize=address,undefined'))
 args=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation']
 def build(text):
  src.write_text(text);subprocess.run(args+flags+[str(src),'-o',exe],check=True)
 build(code)
 if not os.environ.get('IC_TIMER_SKIP_RUN'):
  runner=shlex.split(os.environ.get('IC_TIMER_RUNNER',''))
  subprocess.run(runner+[exe],check=True,timeout=30)
  for label,old,new in [
   ('absent owner still required','if (t) {','assert(t); if (t) {'),
   ('missing warp start retained','s->vm_clock_warp_start = -1;','s->vm_clock_warp_start = 0;'),
   ('fixed shift overwritten','if (icount_enabled() != ICOUNT_ADAPTATIVE)', 'if (icount_enabled() == ICOUNT_ADAPTATIVE)')]:
   assert old in code,label
   build(code.replace(old,new))
   result=subprocess.run(runner+[exe],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
   assert result.returncode and b'Assertion' in result.stdout,(label,result.stdout)
   print('Required negative control rejected:',label,flush=True)

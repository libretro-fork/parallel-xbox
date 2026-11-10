# SPDX-License-Identifier: GPL-2.0-or-later
"""Production PTIMER conversions, scheduling and unconfigured MMIO oracle."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(r/'hw/xbox/nv2a/ptimer.c').read_text().replace('#include "nv2a_int.h"','')
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef uint64_t hwaddr;
typedef struct {bool pending;uint64_t deadline;unsigned mods,dels;} QEMUTimer;
typedef struct {struct {uint32_t pending_interrupts,enabled_interrupts,numerator,denominator;uint64_t alarm_time,time_offset;QEMUTimer timer;} ptimer;struct {uint64_t core_clock_freq;} pramdac;unsigned irqs;} NV2AState;
#define NANOSECONDS_PER_SECOND 1000000000
#define QEMU_CLOCK_VIRTUAL 0
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ROUND_UP(v,a) (((v)+(a)-1)&~((a)-1))
static uint64_t now;static unsigned clock_reads,divides;
static uint64_t qemu_clock_get_ns(int type){assert(type==0);clock_reads++;return now;}
static uint64_t muldiv64(uint64_t a,uint32_t b,uint32_t c){assert(c);divides++;return (__uint128_t)a*b/c;}
static uint64_t muldiv64_round_up(uint64_t a,uint32_t b,uint32_t c){assert(c);divides++;return ((__uint128_t)a*b+c-1)/c;}
static void timer_del(QEMUTimer*t){t->pending=false;t->dels++;}
static void timer_mod(QEMUTimer*t,uint64_t deadline){t->pending=true;t->deadline=deadline;t->mods++;}
static bool timer_pending(QEMUTimer*t){return t->pending;}
static void timer_init_ns(QEMUTimer*t,int clock,void(*cb)(void*),void*opaque){(void)cb;(void)opaque;assert(clock==0);memset(t,0,sizeof(*t));}
static void nv2a_update_irq(NV2AState*d){d->irqs++;}
static void nv2a_reg_log_read(unsigned block,hwaddr addr,unsigned size,uint64_t val){(void)block;(void)addr;(void)size;(void)val;}
static void nv2a_reg_log_write(unsigned block,hwaddr addr,unsigned size,uint64_t val){(void)block;(void)addr;(void)size;(void)val;}
/* PRODUCTION */
static uint64_t ceil_ratio(uint64_t a,uint32_t b,uint32_t c){assert(c);__uint128_t x=(__uint128_t)a*b;return x/c+(x%c!=0);}
int main(void){
 const uint32_t factors[]={0,1,3,17,65535},freqs[]={0,1,17,166000000,333000000};
 const uint64_t times[]={0,1,1000000,1000000000000},ticks[]={0,1,2,32767,1000000};
 unsigned conversions=0,schedules=0,mmio=0;
 const uint64_t mask=UINT64_C(0x1fffffffffffffe0);
 for(unsigned ni=0;ni<5;ni++)for(unsigned di=0;di<5;di++)for(unsigned fi=0;fi<5;fi++)
 for(unsigned ti=0;ti<4;ti++){
  NV2AState d={0};d.ptimer.numerator=factors[ni];d.ptimer.denominator=factors[di];d.pramdac.core_clock_freq=freqs[fi];now=times[ti];
  bool ready=ni&&di&&fi;uint64_t absolute=0;
  if(ready)absolute=(uint64_t)((__uint128_t)((__uint128_t)now*freqs[fi]/1000000000)*factors[di]/factors[ni]);
  divides=clock_reads=0;
  assert(ptimer_get_absolute_clock(&d)==absolute);
  assert(divides==(ready?2u:0u)&&clock_reads==ready);
  for(unsigned k=0;k<5;k++){
   divides=clock_reads=0;uint64_t expected=ready?ceil_ratio(ceil_ratio(ticks[k],factors[ni],factors[di]),1000000000,freqs[fi]):0;
   assert(ptimer_ticks_to_ns(&d,ticks[k])==expected);
   assert(divides==(ready?2u:0u)&&!clock_reads);conversions++;
  }
  for(unsigned enabled=0;enabled<2;enabled++)for(unsigned pending=0;pending<2;pending++)
  for(unsigned offset=0;offset<2;offset++)for(unsigned alarm=0;alarm<4;alarm++){
   d.ptimer.time_offset=offset?12345:0;d.ptimer.enabled_interrupts=enabled;
   d.ptimer.pending_interrupts=0x80;d.irqs=0;
   d.ptimer.timer=(QEMUTimer){.pending=pending,.deadline=777};
   uint64_t reg=((absolute+d.ptimer.time_offset)<<5)&mask;
   uint64_t target=(reg+(alarm==0?32:alarm==1?64:alarm==2?0:(uint64_t)-32))&mask;
   d.ptimer.alarm_time=target;divides=clock_reads=0;
   schedule_qemu_timer(&d);
   if(!ready||!enabled){
    assert(!d.ptimer.timer.pending&&d.ptimer.timer.dels==1&&!d.ptimer.timer.mods);
    assert(d.ptimer.timer.deadline==777&&d.ptimer.alarm_time==target);
    assert(d.ptimer.pending_interrupts==0x80&&!d.irqs&&!divides&&!clock_reads);
   }else{
    uint64_t distance=(target-reg)&mask;bool reached=!distance||distance>mask/2;
    if(reached){
     uint64_t high=reg&~UINT64_C(0xffffffff),low=target&UINT64_C(0xffffffe0);
     if(low<=(reg&UINT64_C(0xffffffe0)))high+=UINT64_C(0x100000000);
     target=(high|low)&mask;distance=(target-reg)&mask;
    }
    uint64_t ns=ceil_ratio(ceil_ratio(distance>>5,factors[ni],factors[di]),1000000000,freqs[fi]);
    assert(d.ptimer.timer.pending&&d.ptimer.timer.mods==1&&!d.ptimer.timer.dels);
    assert(d.ptimer.timer.deadline==now+(ns?ns:1));
    assert(d.ptimer.alarm_time==target&&d.ptimer.pending_interrupts==(0x80u|reached)&&d.irqs==reached);
   }
   schedules++;
  }
  if(!ready){
   const unsigned regs[]={NV_PTIMER_NUMERATOR,NV_PTIMER_DENOMINATOR,NV_PTIMER_TIME_0,NV_PTIMER_TIME_1,NV_PTIMER_ALARM_0,NV_PTIMER_INTR_EN_0,NV_PTIMER_INTR_0};
   for(unsigned k=0;k<7;k++){
    (void)ptimer_read(&d,regs[k],4);
    ptimer_write(&d,regs[k],0,4);
    assert(!d.ptimer.timer.pending);mmio++;
   }
  }
 }
 printf("PASS: %u conversions, %u alarm schedules and %u unconfigured MMIO sequences; zero divisors, rounding, wrap, masks, IRQ and timer ownership\n",conversions,schedules,mmio);
}
'''.replace('/* PRODUCTION */',source)
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='ptimer-clock-') as d:
 p=Path(d);src=p/'test.c';exe=os.environ.get('PTIMER_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('PTIMER_CFLAGS','-fsanitize=address,undefined'))
 args=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r)]
 def build(text):
  src.write_text(text);subprocess.run(args+flags+[str(src),'-o',exe],check=True)
 build(code)
 if not os.environ.get('PTIMER_SKIP_RUN'):
  runner=shlex.split(os.environ.get('PTIMER_RUNNER',''))
  subprocess.run(runner+[exe],check=True,timeout=60)
  for label,old,new in [
   ('zero-divisor conversions','if (!ptimer_clock_configured(d)) {\n        return 0;','if (false) {\n        return 0;'),
   ('stopped clock alarm scheduled','if (!ptimer_clock_configured(d)) {\n        timer_del', 'if (false) {\n        timer_del'),
   ('configured conversion rounded down','muldiv64_round_up(internal_ticks,','muldiv64(internal_ticks,')]:
   assert old in code,label
   build(code.replace(old,new))
   result=subprocess.run(runner+[exe],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
   assert result.returncode and b'Assertion' in result.stdout,(label,result.stdout)
   print('Required negative control rejected:',label,flush=True)

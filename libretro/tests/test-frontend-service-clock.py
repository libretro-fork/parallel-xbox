# SPDX-License-Identifier: GPL-2.0-or-later
"""Production service clock: frontend steps, state rewind and non-spinning polls."""
from pathlib import Path
import re, subprocess, sys, tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def fn(path,name):
 s=(root/path).read_text();m=re.search(r'^(?:static )?(?:inline )?(?:void|bool|int|int64_t|gint64) '+name+r'\(',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
parts=[fn('libretro/glib/gcore.c',n) for n in ['g_libretro_get_time_ns','g_libretro_advance_time_ns','g_get_monotonic_time']]
parts += [fn('util/qemu-timer-common.c','qemu_libretro_service_clock_ns')]
parts += [fn('include/qemu/timer.h',n) for n in ['get_clock','get_clock_realtime','cpu_get_host_ticks']]
parts += [fn('ui/libretro.c','lr_advance_service_clock')]
parts += [fn('util/qemu-timer.c',n) for n in ['qemu_clock_use_for_deadline','qemu_clock_get_ns','qemu_timeout_ns_to_ms','qemu_poll_ns']]
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
typedef int64_t gint64;
typedef unsigned guint;
typedef struct {int fd;} GPollFD;
typedef enum {QEMU_CLOCK_REALTIME,QEMU_CLOCK_VIRTUAL,QEMU_CLOCK_HOST,QEMU_CLOCK_VIRTUAL_RT,QEMU_CLOCK_MAX} QEMUClockType;
#define SCALE_MS 1000000
#define MIN(a,b) ((a)<(b)?(a):(b))
#define XBOX_NO_HR_TIMER (-2)
static __attribute__((unused)) bool xemu_cpu_boost=true;
static __attribute__((unused)) int xbox_win_poll_hr(GPollFD*f,guint n,int64_t ns){(void)f;(void)n;(void)ns;return XBOX_NO_HR_TIMER;}
#define LR_FRAME_NS_NUM 50050000LL
#define LR_FRAME_NS_DEN 3
#define g_return_if_fail(x) do {if(!(x))return;}while(0)
static gint64 g_libretro_time_ns;
static int64_t guest_now;
static unsigned polls;
static int last_ms;
static int64_t cpus_get_virtual_clock(void){return guest_now;}
static int64_t cpu_get_clock(void){return guest_now;}
static int g_poll(GPollFD*f,guint n,int ms){assert(!f&&!n);polls++;last_ms=ms;return 7;}
/* PRODUCTION */
int main(void){
 assert(get_clock()==0 && get_clock_realtime()==0 && cpu_get_host_ticks()==0);
 for(uint64_t frame=0;frame<60000;frame++){
  lr_advance_service_clock();
  uint64_t expected=(frame+1)*UINT64_C(1001000000)/60;
  assert((uint64_t)get_clock()==expected);
  assert(g_get_monotonic_time()==(int64_t)(expected/1000));
  assert(qemu_clock_get_ns(QEMU_CLOCK_REALTIME)==(int64_t)expected);
  guest_now=frame%2?0:123456789;
  assert(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)==guest_now);
  assert(qemu_clock_get_ns(QEMU_CLOCK_HOST)==guest_now);
  assert(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT)==guest_now);
  assert((uint64_t)get_clock()==expected);
 }
 int64_t before=get_clock();g_libretro_advance_time_ns(-1);assert(get_clock()==before);
 for(int type=0;type<QEMU_CLOCK_MAX;type++)assert(!qemu_clock_use_for_deadline(type));
 const int64_t waits[]={-1,0,1,999,1000000,1000001,INT64_MAX};
 for(unsigned i=0;i<sizeof(waits)/sizeof(waits[0]);i++){
  polls=0;assert(qemu_poll_ns(NULL,0,waits[i])==7 && polls==1);
  assert(last_ms==qemu_timeout_ns_to_ms(waits[i]));
  assert(get_clock()==before);
 }
 puts("Frontend service clock: 60000 exact NTSC steps, guest rewinds, aliases, deadline exclusion and non-spinning relative waits PASS");
}
"""
code=code.replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='frontend-clock-') as t:
 t=Path(t);(t/'test.c').write_text(code);binary=t/'test.exe'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(t/'test.c'),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True,timeout=30)

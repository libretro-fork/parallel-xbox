# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production RTC virtual-time reads and legacy-state conversion."""
from pathlib import Path
import subprocess, sys, tempfile
root = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
def function(source, name):
    start = source.index(name + '(')
    start = source.rfind('\n', 0, start) + 1
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
rtc = (root / 'hw/rtc/mc146818rtc.c').read_text()
config = (root / 'system/rtc.c').read_text()
assert '.name = "mc146818rtc",\n    .version_id = 4,' in rtc
assert 'rtc_clock = QEMU_CLOCK_VIRTUAL;' in config
assert 'rtc_ref_start_datetime = 946684800;' in config
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#define NANOSECONDS_PER_SECOND INT64_C(1000000000)
#define QEMU_CLOCK_VIRTUAL 1
#define LOST_TICK_POLICY_SLEW 1
typedef int QEMUClockType;
static int rtc_clock=QEMU_CLOCK_VIRTUAL;
static time_t rtc_ref_start_datetime=946684800;
static int64_t guest_now;
static unsigned periodic, updates, coalesced;
static int64_t qemu_clock_get_ns(int clock){assert(clock==QEMU_CLOCK_VIRTUAL);return guest_now;}
static int64_t qemu_clock_get_ms(int clock){return qemu_clock_get_ns(clock)/1000000;}
typedef struct {
 uint64_t base_rtc,last_update,next_alarm_time;
 int64_t offset,next_periodic_time,update_deadline;
 uint32_t period;
 int lost_tick_policy;
 uint64_t saved_cmos_second;
} MC146818RtcState;
static void rtc_set_time(MC146818RtcState*s){s->base_rtc=s->saved_cmos_second;s->last_update=guest_now;}
static uint32_t rtc_periodic_clock_ticks(MC146818RtcState*s){(void)s;return 32;}
static void periodic_timer_update(MC146818RtcState*s,int64_t now,uint32_t old,bool change){
 (void)old;assert(!change);periodic++;s->next_periodic_time=now+976563;
}
static void check_update_timer(MC146818RtcState*s){updates++;s->update_deadline=guest_now+1000000000;s->next_alarm_time=guest_now+5000000000;}
static int64_t get_max_clock_jump(void){return 1000000000;}
static void rtc_coalesced_timer_update(MC146818RtcState*s){(void)s;coalesced++;}
/* PRODUCTION */
int main(void){
 unsigned cases=0;
 for(unsigned phase=0;phase<1000;phase++){
  guest_now=INT64_C(9000000000)+phase*100003;
  assert(qemu_ref_timedate(rtc_clock)==946684800+guest_now/1000000000);
  MC146818RtcState old={.base_rtc=1700000000,.last_update=INT64_C(1700000000123456789),
   .offset=phase,.next_periodic_time=INT64_C(1700000001000000000),
   .next_alarm_time=INT64_C(1700000010000000000),.saved_cmos_second=1700001234,
   .lost_tick_policy=phase%2};
  periodic=updates=coalesced=0;
  assert(!rtc_post_load(&old,3));
  assert(get_guest_rtc_ns(&old)==INT64_C(1700001234000000000));
  assert(old.last_update==(uint64_t)guest_now && old.offset==0);
  assert(old.next_periodic_time==guest_now+976563 && old.update_deadline==guest_now+1000000000);
  assert(old.next_alarm_time==(uint64_t)guest_now+5000000000);
  assert(periodic==1 && updates==1 && coalesced==phase%2);
  int64_t date=get_guest_rtc_ns(&old);guest_now+=123456789;
  assert(get_guest_rtc_ns(&old)==(uint64_t)(date+123456789));
  MC146818RtcState state=old;state.offset=phase;
  state.next_periodic_time=guest_now+999;state.update_deadline=guest_now+555555;
  int64_t before=get_guest_rtc_ns(&state);
  uint64_t alarm=state.next_alarm_time;
  periodic=updates=coalesced=0;
  assert(!rtc_post_load(&state,4));
  assert(get_guest_rtc_ns(&state)==(uint64_t)before);
  assert(state.next_periodic_time==guest_now+999 && state.update_deadline==guest_now+555555);
  assert(state.next_alarm_time==alarm && !periodic && !updates);
  cases+=2;
 }
 /* Corrupt/far deadlines must rearm without signed subtraction overflow.
  * Nearby future and overdue deadlines retain their exact virtual phase. */
 const int64_t deadlines[]={INT64_MIN,INT64_MAX,0,8999999999LL,9000000000LL,9000000999LL};
 guest_now=9000000000LL;
 for(unsigned i=0;i<sizeof(deadlines)/sizeof(deadlines[0]);i++){
  MC146818RtcState state={.base_rtc=946684800,.last_update=guest_now,
    .next_periodic_time=deadlines[i],.update_deadline=guest_now+555555};
  periodic=updates=coalesced=0;
  assert(!rtc_post_load(&state,4));
  if(i<3)assert(periodic==1 && state.next_periodic_time==guest_now+976563);
  else assert(!periodic && state.next_periodic_time==deadlines[i]);
  assert(!updates && state.update_deadline==guest_now+555555);
  cases++;
 }
 printf("RTC virtual clock: %u legacy/new-state/calendar/phase cases passed\n",cases);
}
"""
parts = [function(config, 'qemu_ref_timedate'), function(rtc, 'get_guest_rtc_ns'), function(rtc, 'rtc_post_load')]
code=code.replace('/* PRODUCTION */', '\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='rtc-virtual-clock-') as temp:
    temp=Path(temp); source=temp/'test.c'; source.write_text(code); binary=temp/'test.exe'
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)

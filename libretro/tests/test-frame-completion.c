/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
static int nv2a_libretro_waiter;
static unsigned work, display_work, display_calls, waits, fences, checks, stop_after;
static bool running, fenced, racing, spurious;
static void retro_atomic_store_relaxed_int(int *p, int value) {
    assert(p==&nv2a_libretro_waiter);*p=value;fenced=false;
}
static void retro_atomic_thread_fence_seq_cst(void) {
    assert(nv2a_libretro_waiter==1);fenced=true;fences++;
    if(racing) { work++;racing=false; }
}
static bool runstate_is_running(void) { return running; }
bool nv2a_libretro_busy(void) {
    assert(nv2a_libretro_waiter==1 && fenced);checks++;return work!=0;
}
void nv2a_libretro_display_check(void) {
    assert(!nv2a_libretro_waiter && running && !work);display_calls++;work=display_work;
}
static void main_loop_wait(bool nonblocking) {
    assert(!nonblocking && nv2a_libretro_waiter==1 && fenced && running && work);
    waits++;assert(waits<100);
    if(!spurious || !(waits&1)) work--;
    if(stop_after && waits==stop_after) running=false;
}
/* PRODUCTION */
int main(void) {
    unsigned cases=0;
    for(unsigned pending=0;pending<9;pending++) for(unsigned display=0;display<9;display++)
    for(unsigned race=0;race<2;race++) for(unsigned wake=0;wake<2;wake++)
    for(unsigned stop=0;stop<3;stop++) {
        work=pending;display_work=display;display_calls=waits=fences=checks=0;
        racing=race;spurious=wake;running=stop!=2;stop_after=stop==1?2:0;
        nv2a_libretro_waiter=0;fenced=false;
        lr_finish_frame();
        assert(!nv2a_libretro_waiter && !fenced);
        assert(fences==waits+2);
        if(running) { assert(!work && display_calls==1);assert(checks==waits+2); }
        else { assert(display_calls<=1);assert(stop==2 || waits==stop_after); }
        if(stop==2) assert(!waits && !checks && !display_calls);
        cases++;
    }
    printf("frame handoff: %u idle/busy/display/racing-wake/spurious-wake/stop cases passed\n",cases);
    return 0;
}

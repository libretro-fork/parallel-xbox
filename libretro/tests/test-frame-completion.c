/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The frame's end (ui/libretro.c lr_finish_frame), compiled from the
 * production source against a model of the GPU's thread: one request,
 * then waits until the thread says the display is drawn, each wait after
 * the flag and a full fence; none with no GPU thread or the machine
 * stopped; a stop while waiting ends it. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
static int nv2a_libretro_waiter;
static unsigned work, waits, fences, checks, requests, stop_after;
static int end_req, end_done;
static bool running, fenced, racing, spurious, gpu_thread;
static void retro_atomic_store_relaxed_int(int *p, int value) {
    assert(p==&nv2a_libretro_waiter);*p=value;fenced=false;
}
static void retro_atomic_thread_fence_seq_cst(void) {
    assert(nv2a_libretro_waiter==1);fenced=true;fences++;
    /* the thread finishes between the fence and the look */
    if(racing && work) { work=0;end_done=end_req;racing=false; }
}
static bool runstate_is_running(void) { return running; }
int nv2a_libretro_frame_end(void) {
    assert(!nv2a_libretro_waiter && running);requests++;
    if(!gpu_thread) return 0;
    end_req++;
    if(!work) end_done=end_req;
    return end_req;
}
bool nv2a_libretro_frame_end_done(int end) {
    assert(nv2a_libretro_waiter==1 && fenced && end==end_req);checks++;
    return end_done==end;
}
static void main_loop_wait(bool nonblocking) {
    assert(!nonblocking && nv2a_libretro_waiter==1 && fenced && running);
    assert(end_done!=end_req);
    waits++;assert(waits<100);
    if(!spurious || !(waits&1)) { if(work && !--work) end_done=end_req; }
    if(stop_after && waits==stop_after) running=false;
}
/* PRODUCTION */
int main(void) {
    unsigned cases=0;
    for(unsigned pending=0;pending<9;pending++)
    for(unsigned race=0;race<2;race++) for(unsigned wake=0;wake<2;wake++)
    for(unsigned stop=0;stop<3;stop++) for(unsigned gpu=0;gpu<2;gpu++) {
        work=pending;waits=fences=checks=requests=0;end_req=end_done=7;
        racing=race;spurious=wake;running=stop!=2;stop_after=stop==1?2:0;
        gpu_thread=gpu;nv2a_libretro_waiter=0;fenced=false;
        lr_finish_frame();
        assert(!nv2a_libretro_waiter && !fenced);
        if(stop==2) { assert(!requests && !waits && !checks); cases++; continue; }
        assert(requests==1);
        if(!gpu) { assert(!waits && !checks && !fences); cases++; continue; }
        assert(fences==waits+1 && checks<=fences);
        if(running) assert(end_done==end_req && checks==waits+1);
        else assert(waits==stop_after);
        cases++;
    }
    printf("frame handoff: %u idle/busy/racing-wake/spurious-wake/stop/no-GPU cases passed\n",cases);
    return 0;
}

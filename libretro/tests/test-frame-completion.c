/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Frontend finish requests work without waiting for the FIFO owner. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define assert(x) do { if (!(x)) { fprintf(stderr,"frame handoff: %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
static bool running,gpu_thread;
static unsigned work,requests,waits;
static int end_req,end_done;
static bool runstate_is_running(void) { return running; }
int nv2a_libretro_frame_end(void) {
    assert(running);requests++;
    if(!gpu_thread)return 0;
    end_req++;
    if(!work)end_done=end_req;
    return end_req;
}
/* A future synchronous wait must fail, even if it eventually completes. */
static void __attribute__((unused)) main_loop_wait(bool nonblocking) {
    (void)nonblocking;waits++;
}
/* PRODUCTION */
int main(void) {
    unsigned cases=0;
    for(unsigned run=0;run<2;run++)for(unsigned gpu=0;gpu<2;gpu++)
    for(unsigned pending=0;pending<9;pending++) {
        running=run;gpu_thread=gpu;work=pending;
        requests=waits=0;end_req=end_done=7;
        lr_finish_frame();
        assert(requests==run && !waits && work==pending);
        assert(end_req==(run&&gpu?8:7));
        assert(end_done==(run&&gpu&&!pending?8:7));
        cases++;
    }
    printf("frame handoff: %u running/stopped, absent/busy/idle owner cases; no waits or consumed producer work PASS\n",cases);
}

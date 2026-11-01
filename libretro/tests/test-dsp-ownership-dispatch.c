/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define NUM_MIXBINS 32
#define NUM_SAMPLES_PER_FRAME 32
#define NV_PAPU_GPRST_GPRST 1
#define NV_PAPU_GPRST_GPDSPRST 2
#define MCPX_APU_DEBUG_MON_GP_OR_EP 1
#define LR_DSP_FENCE_TICK 5
typedef struct {struct {uint32_t gp_rst,ep_rst;} dsp_state;struct {int point;} monitor;int ep_frame_div;} MCPXAPUState;
static bool ownership_enabled=true;
static unsigned ownership_cpu_depth;
static int ownership_ready=1,ownership_posted=7,ownership_done=6,ownership_div,ownership_work,bus_lease;
static uint64_t ownership_pages[512],ownership_tables[512];
static bool pending,admit,worker_role;
static unsigned gp_calls,ep_calls,overlap_calls,fences,protects,wakes;
static int retro_atomic_load_acquire_int(int*p){return *p;}
static int retro_atomic_load_relaxed_int(int*p){return *p;}
static void retro_atomic_store_release_int(int*p,int v){*p=v;if(p==&ownership_posted)pending=true;}
static void retro_eventcount_notify(int*p){wakes++;}
static bool bus_lease_try_dsp(int*p){return admit;}
static void bus_lease_dsp_done(int*p){}
static bool ownership_reserve_tables(MCPXAPUState*d){assert(!pending);return true;}
static bool ownership_candidate(MCPXAPUState*d){assert(!pending);return true;}
static bool gpu_try_dsp_pages(void){return true;}
static void gpu_release_dsp_pages(void){}
static void own_protect(MCPXAPUState*d){assert(!pending);protects++;}
static void dsp_run_gp(MCPXAPUState*d,int64_t input[32][32]){gp_calls++;if(pending)overlap_calls++;}
static void dsp_run_ep(MCPXAPUState*d,int div){assert(!pending || worker_role);ep_calls++;}
static void lr_dsp_ownership_quiesce(void){fences++;pending=false;ownership_done=ownership_posted;}
static void lr_dsp_ownership_fence(unsigned reason){assert(reason==5);lr_dsp_ownership_quiesce();}
/* PRODUCTION POST */
static void reset(void){gp_calls=ep_calls=overlap_calls=fences=protects=wakes=0;pending=true;admit=true;ownership_done=ownership_posted-1;}
int main(void){
 MCPXAPUState d={.dsp_state={3,3},.monitor={1}};static int64_t input[32][32];
 for(unsigned i=0;i<2000;i++){
  reset();d.ep_frame_div=1;mcpx_apu_dsp_post(&d,input);
  assert(gp_calls==1 && overlap_calls==1 && fences==1 && ep_calls==1 && !pending && !wakes);
  reset();d.ep_frame_div=0;mcpx_apu_dsp_post(&d,input);
  assert(gp_calls==1 && overlap_calls==1 && fences==1 && !ep_calls && pending && wakes==1 && protects==1);
  assert(ownership_div==0);
  worker_role=true;dsp_run_ep(&d,ownership_div);worker_role=false;pending=false;ownership_done=ownership_posted;
  reset();admit=false;mcpx_apu_dsp_post(&d,input);
  assert(gp_calls==1 && overlap_calls==1 && fences==1 && ep_calls==1 && !pending && !wakes);
 }
 reset();admit=false;d.monitor.point=2;mcpx_apu_dsp_post(&d,input);assert(gp_calls==1 && !overlap_calls && ep_calls==1 && !pending);
 reset();d.monitor.point=1;d.dsp_state.ep_rst=0;mcpx_apu_dsp_post(&d,input);assert(gp_calls==1 && !overlap_calls && ep_calls==1 && !pending && !wakes);
 puts("PASS: production post dispatcher overlaps GP with pending EP, drains before queue/footprint changes, posts EP alone and executes fallback/unsupported monitors once (2000 rounds)");
}

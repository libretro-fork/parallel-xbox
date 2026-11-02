/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MCPX_HW_MAX_VOICES 256
#define NUM_MIXBINS 32
#define NUM_SAMPLES_PER_FRAME 32
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DPRINTF(...) ((void)0)
#define NV_PAVS_VOICE_TAR_PITCH_LINK 1
#define NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE 1
#define NV_PAVS_VOICE_PAR_STATE 2
#define NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE 1
#define SE2FE_IDLE_VOICE 7
#define MCPX_APU_DEBUG_MON_VP 1
typedef uint64_t hwaddr;
typedef struct {uint16_t voice;int list;} VoiceWorkItem;
typedef struct {uint64_t before;VoiceWorkItem queue[256];uint64_t after;int queue_len;} VoiceWorkDispatch;
typedef struct {struct {VoiceWorkDispatch voice_work_dispatch;int64_t sample_buf[32][2];}vp;
 struct {int point;}monitor;unsigned regs[9],link[256];bool active[256],locked[256];} MCPXAPUState;
static const struct {int top,current,next;} voice_list_regs[3]={{0,1,2},{3,4,5},{6,7,8}};
static VoiceWorkItem trace[768],expected[768];static unsigned jobs,expected_jobs,dispatches,queued_at_dispatch,jobs_before_dispatch,monitors;static int64_t monitor_sum;
static unsigned apu_reg_r(MCPXAPUState*d,int reg){return d->regs[reg];}
static void apu_reg_w(MCPXAPUState*d,int reg,unsigned value){d->regs[reg]=value;}
static unsigned voice_get_mask(MCPXAPUState*d,uint16_t v,unsigned reg,unsigned mask){assert(v<256&&mask==1);return reg==1?d->link[v]:d->active[v];}
static bool is_voice_locked(MCPXAPUState*d,uint16_t v){return d->locked[v];}
static void fe_method(MCPXAPUState*d,int method,uint16_t v){(void)d;assert(method==7&&v<256);}
static void voice_process(MCPXAPUState*d,int64_t mix[][32],int64_t samples[][2],uint16_t v,int list){
 assert(jobs<768&&samples==d->vp.sample_buf);trace[jobs++]=(VoiceWorkItem){v,list};if(list==2&&v==0){mix[1][0]+=mix[0][0];mix[0][0]=0;}mix[0][0]+=v+1+list*256;samples[0][0]+=v+1+list*256;
}
/* HELPERS */
static void voice_work_dispatch(MCPXAPUState*d,int64_t mix[][32]){dispatches++;queued_at_dispatch=d->vp.voice_work_dispatch.queue_len;jobs_before_dispatch=jobs;voice_work_run_serial(d,mix);}
static void mcpx_apu_monitor_push_mix(const int64_t samples[][2],int n){assert(n==32);monitors++;monitor_sum=samples[0][0];}
/* FRAME */
int main(void){unsigned cases=0,overflow_cases=0;const unsigned lengths[]={0,1,2,127,128,255,256};
 for(unsigned li=0;li<ARRAY_SIZE(lengths);li++)for(unsigned heads=0;heads<8;heads++)
 for(unsigned cycle=0;cycle<2;cycle++)for(unsigned monitor=0;monitor<2;monitor++)for(unsigned skip=0;skip<2;skip++){
  MCPXAPUState d={0};int64_t mix[32][32]={0};unsigned length=lengths[li];
  d.monitor.point=monitor;d.vp.voice_work_dispatch.before=d.vp.voice_work_dispatch.after=UINT64_C(0x917654329abcdef0);
  for(unsigned v=0;v<256;v++){d.active[v]=!skip||v%3!=0;d.locked[v]=skip&&v%5==0;d.link[v]=v+1<length?v+1:cycle?0:0xffff;}
  jobs=expected_jobs=dispatches=monitors=0;monitor_sum=0;int64_t sum=0,bin=0,consumed=0;unsigned final[3];
  for(unsigned list=0;list<3;list++){
   unsigned v=length&&(heads&(1u<<list))?0:0xffff;d.regs[list*3]=v;
   for(unsigned count=0;v!=0xffff&&count<256;count++){if(d.active[v]&&!d.locked[v]){expected[expected_jobs++]=(VoiceWorkItem){v,list};if(list==2&&v==0){consumed+=bin;bin=0;}bin+=v+1+list*256;sum+=v+1+list*256;}v=d.link[v];}
   final[list]=v;
  }
  mcpx_apu_vp_frame(&d,mix);
  assert(jobs==expected_jobs&&dispatches==1&&!d.vp.voice_work_dispatch.queue_len);
  for(unsigned i=0;i<jobs;i++)assert(trace[i].voice==expected[i].voice&&trace[i].list==expected[i].list);
  if(jobs>256){assert(jobs_before_dispatch==jobs&&!queued_at_dispatch);overflow_cases++;}
  else assert(!jobs_before_dispatch&&queued_at_dispatch==jobs);
  assert(monitors==monitor&&mix[0][0]==(monitor?0:bin)&&mix[1][0]==(monitor?0:consumed)&&d.vp.sample_buf[0][0]==(monitor?0:sum));
  if(monitor)assert(monitor_sum==sum);
  for(unsigned list=0;list<3;list++)assert(d.regs[list*3+1]==final[list]);
  assert(d.vp.voice_work_dispatch.before==UINT64_C(0x917654329abcdef0)&&d.vp.voice_work_dispatch.after==UINT64_C(0x917654329abcdef0));cases++;
 }
 printf("PASS: %u voice-list capacity schedules, %u overflow frames; up to 768 ordered visits, cycles, shared lists, idle/locked voices, mix preservation, monitor and queue canaries\n",cases,overflow_cases);
}

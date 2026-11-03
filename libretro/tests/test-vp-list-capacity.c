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
#define NV_PAVS_VOICE_PAR_STATE_PAUSED 4
#define NV_PAVS_VOICE_CFG_FMT 3
#define NV_PAVS_VOICE_CFG_FMT_MULTIPASS 2
#define NV_PAVS_VOICE_CFG_FMT_MULTIPASS_BIN 3
#define SE2FE_IDLE_VOICE 7
#define MCPX_APU_DEBUG_MON_VP 1
typedef uint64_t hwaddr;
typedef struct {uint16_t voice;int list;} VoiceWorkItem;
typedef struct {uint64_t before;VoiceWorkItem queue[256];uint64_t after;int queue_len;} VoiceWorkDispatch;
typedef struct {struct {VoiceWorkDispatch voice_work_dispatch;int64_t sample_buf[32][2];}vp;
 struct {int point;}monitor;unsigned regs[9],link[256];bool active[256],paused[256],locked[256],multipass[256];unsigned bins[256];} MCPXAPUState;
static const struct {int top,current,next;} voice_list_regs[3]={{0,1,2},{3,4,5},{6,7,8}};
static VoiceWorkItem trace[768],expected[768];static unsigned jobs,expected_jobs,dispatches,queued_at_dispatch,jobs_before_dispatch,monitors;static int64_t monitor_sum;
static unsigned process_calls,idle_calls;
static bool testing_lookahead;static unsigned lookahead_reads,descriptor_reads;
static unsigned apu_reg_r(MCPXAPUState*d,int reg){return d->regs[reg];}
static void apu_reg_w(MCPXAPUState*d,int reg,unsigned value){d->regs[reg]=value;}
static unsigned voice_get_mask(MCPXAPUState*d,uint16_t v,unsigned reg,unsigned mask){
 assert(v<256);descriptor_reads++;if(testing_lookahead)assert(++lookahead_reads<=512);
 if(reg==3)return mask==2?d->multipass[v]:d->bins[v];
 if(reg==2){unsigned state=(d->active[v]?NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE:0)|(d->paused[v]?NV_PAVS_VOICE_PAR_STATE_PAUSED:0);return (state&mask)>>__builtin_ctz(mask);}
 assert(mask==1);return d->link[v];
}
static bool is_voice_locked(MCPXAPUState*d,uint16_t v){assert(v<256);return d->locked[v];}
static void fe_method(MCPXAPUState*d,int method,uint16_t v){(void)d;assert(method==7&&v<256);idle_calls++;}
static void voice_process(MCPXAPUState*d,int64_t mix[][32],int64_t samples[][2],uint16_t v,int list){
 assert(v<256&&jobs<768&&samples==d->vp.sample_buf);process_calls++;if(d->paused[v])return;trace[jobs++]=(VoiceWorkItem){v,list};if(list==2&&v==0){mix[1][0]+=mix[0][0];mix[0][0]=0;}mix[0][0]+=v+1+list*256;samples[0][0]+=v+1+list*256;
}
/* HELPERS */
static void voice_work_dispatch(MCPXAPUState*d,int64_t mix[][32]){dispatches++;queued_at_dispatch=d->vp.voice_work_dispatch.queue_len;jobs_before_dispatch=jobs;voice_work_run_serial(d,mix);}
static void mcpx_apu_monitor_push_mix(const int64_t samples[][2],int n){assert(n==32);monitors++;monitor_sum=samples[0][0];}
/* FRAME */
static void lookahead_tests(void){
 unsigned cases=0;const unsigned starts[]={0,63,64,127,128,191,192,255},lengths[]={0,1,2,63,64,65,127,128,129,255,256};
 testing_lookahead=true;
 for(unsigned si=0;si<ARRAY_SIZE(starts);si++)for(unsigned li=0;li<ARRAY_SIZE(lengths);li++)
 for(unsigned cycle=0;cycle<2;cycle++)for(unsigned target=0;target<4;target++)for(unsigned bin=0;bin<32;bin+=31){
  MCPXAPUState d={0};unsigned start=starts[si],len=lengths[li];
  for(unsigned i=0;i<len;i++)d.link[(start+i)%256]=i+1<len?(start+i+1)%256:cycle?start:0xffff;
  int pos=!len||!target?-1:target==1?0:target==2?(int)(len/2):(int)len-1;
  if(pos>=0){unsigned v=(start+(unsigned)pos)%256;d.multipass[v]=true;d.bins[v]=bin;}
  uint16_t dst=0x1234;lookahead_reads=0;int result=peek_ahead_multipass_bin(&d,len?start:0xffff,&dst);
  assert(result==(pos>0?(int)bin:-1)&&dst==(pos>0?(start+(unsigned)pos)%256:0xffff));cases++;
 }
 MCPXAPUState d={0};d.link[0]=1;d.link[1]=2;d.link[2]=1;uint16_t dst=0;lookahead_reads=0;
 assert(peek_ahead_multipass_bin(&d,0,&dst)==-1&&dst==0xffff&&lookahead_reads==512);
 const unsigned invalid[]={256,65534,65535};for(unsigned i=0;i<ARRAY_SIZE(invalid);i++){lookahead_reads=0;dst=0;assert(peek_ahead_multipass_bin(&d,invalid[i],&dst)==-1&&dst==0xffff&&!lookahead_reads);}
 testing_lookahead=false;
 printf("PASS: %u multipass lookahead schedules, valid first/later readers, empty/shared/cyclic chains, lollipop cap and invalid-handle bounds\n",cases);
}
static void invalid_list_tests(void){
 const unsigned invalid[]={256,257,1023,65534,0x10000,0x100ff,0x1ffff,UINT32_MAX};
 const unsigned valid[]={0,63,64,255};unsigned cases=0;
 for(unsigned bad=0;bad<ARRAY_SIZE(invalid);bad++)for(unsigned vi=0;vi<ARRAY_SIZE(valid);vi++)
 for(unsigned target=0;target<3;target++)for(unsigned tail=0;tail<2;tail++)
 for(unsigned monitor=0;monitor<2;monitor++)for(unsigned skip=0;skip<3;skip++){
  /* Next-link fields are 16-bit; list heads are full MMIO register values. */
  if(tail&&invalid[bad]>0xffff)continue;
  MCPXAPUState d={0};int64_t mix[32][32]={0};unsigned first=valid[vi];
  d.monitor.point=monitor;d.vp.voice_work_dispatch.before=d.vp.voice_work_dispatch.after=UINT64_C(0x917654329abcdef0);
  for(unsigned v=0;v<256;v++){d.active[v]=true;d.link[v]=0xffff;}
  if(tail){d.link[first]=invalid[bad];d.active[first]=skip!=1;d.locked[first]=skip==2;}
  jobs=expected_jobs=dispatches=monitors=descriptor_reads=0;monitor_sum=0;int64_t sum=0,bin=0,consumed=0;
  for(unsigned list=0;list<3;list++){
   unsigned v=list==target?(tail?first:invalid[bad]):7;
   d.regs[list*3]=v;d.regs[list*3+2]=0x31415926;
   if(v<256&&d.active[v]&&!d.locked[v]){
    expected[expected_jobs++]=(VoiceWorkItem){v,list};
    if(list==2&&v==0){consumed+=bin;bin=0;}
    bin+=v+1+list*256;sum+=v+1+list*256;
   }
  }
  mcpx_apu_vp_frame(&d,mix);
  assert(descriptor_reads==(tail?6u:4u));
  assert(jobs==expected_jobs&&dispatches==1&&!d.vp.voice_work_dispatch.queue_len);
  for(unsigned i=0;i<jobs;i++)assert(trace[i].voice==expected[i].voice&&trace[i].list==expected[i].list);
  assert(monitors==monitor&&mix[0][0]==(monitor?0:bin)&&mix[1][0]==(monitor?0:consumed));
  assert(d.vp.sample_buf[0][0]==(monitor?0:sum));if(monitor)assert(monitor_sum==sum);
  for(unsigned list=0;list<3;list++){
   assert(d.regs[list*3+1]==(list==target?invalid[bad]:0xffff));
   assert(d.regs[list*3+2]==(list==target?(tail?invalid[bad]:0x31415926):0xffff));
  }
  assert(d.vp.voice_work_dispatch.before==UINT64_C(0x917654329abcdef0)&&d.vp.voice_work_dispatch.after==UINT64_C(0x917654329abcdef0));cases++;
 }
 printf("PASS: %u invalid list heads/links rejected before descriptor access; upper bits retained, other lists and valid prefixes preserved\n",cases);
}
static void paused_list_tests(void){
 const unsigned voices[]={0,63,64,255};unsigned cases=0;
 for(unsigned pause=0;pause<16;pause++)for(unsigned inactive=0;inactive<4;inactive++)
 for(unsigned locked=0;locked<4;locked++)for(unsigned heads=0;heads<8;heads++)
 for(unsigned cycle=0;cycle<2;cycle++)for(unsigned monitor=0;monitor<2;monitor++){
  MCPXAPUState d={0};int64_t mix[32][32]={0};unsigned final[3],idle=0;
  d.monitor.point=monitor;
  for(unsigned i=0;i<4;i++){
   unsigned v=voices[i];d.paused[v]=pause&(1u<<i);
   d.active[v]=inactive==0||(inactive==1?i%2:inactive==2?!(i%2):i!=0);
   d.locked[v]=locked==3||(locked==1?i%2:locked==2?!(i%2):false);
   d.link[v]=i<3?voices[i+1]:cycle?voices[0]:0xffff;
  }
  jobs=expected_jobs=dispatches=monitors=process_calls=idle_calls=0;monitor_sum=0;
  int64_t sum=0,bin=0,consumed=0;
  for(unsigned list=0;list<3;list++){
   unsigned v=heads&(1u<<list)?voices[0]:0xffff;d.regs[list*3]=v;
   for(unsigned count=0;v!=0xffff&&count<256;count++){
    if(!d.active[v])idle++;
    else if(!d.locked[v]&&!d.paused[v]){
     expected[expected_jobs++]=(VoiceWorkItem){v,list};
     if(list==2&&v==0){consumed+=bin;bin=0;}
     bin+=v+1+list*256;sum+=v+1+list*256;
    }
    v=d.link[v];
   }
   final[list]=v;
  }
  mcpx_apu_vp_frame(&d,mix);
  assert(jobs==expected_jobs&&process_calls==jobs&&idle_calls==idle&&dispatches==1&&!d.vp.voice_work_dispatch.queue_len);
  for(unsigned i=0;i<jobs;i++)assert(trace[i].voice==expected[i].voice&&trace[i].list==expected[i].list);
  if(jobs>256)assert(jobs_before_dispatch==jobs&&!queued_at_dispatch);
  else assert(!jobs_before_dispatch&&queued_at_dispatch==jobs);
  assert(monitors==monitor&&mix[0][0]==(monitor?0:bin)&&mix[1][0]==(monitor?0:consumed));
  assert(d.vp.sample_buf[0][0]==(monitor?0:sum));if(monitor)assert(monitor_sum==sum);
  for(unsigned list=0;list<3;list++)assert(d.regs[list*3+1]==final[list]);
  cases++;
 }
 printf("PASS: %u paused-list schedules; no paused jobs, preserved links/idle traps/order/mixes and bounded shared/cyclic lists\n",cases);
}
int main(void){lookahead_tests();invalid_list_tests();paused_list_tests();unsigned cases=0,overflow_cases=0;const unsigned lengths[]={0,1,2,127,128,255,256};
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

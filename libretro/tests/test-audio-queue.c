/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <retro_atomic.h>
#define NUM_SAMPLES_PER_FRAME 32
#define MCPX_HW_MAX_VOICES 256
#define MCPX_APU_MONITOR_STATE_FRAMES 2048
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef struct {struct {struct {int64_t pitch;} filters[256];}vp;
 struct {uint32_t frames;bool loaded;int16_t samples[4096];}saved_output;} MCPXAPUState;
typedef int Error;
static void error_setg(Error **err,const char *text){(void)err;(void)text;abort();}
static void mcpx_apu_dsp_post_silence(MCPXAPUState *d);
/* MONITOR */
static void mcpx_apu_dsp_post_silence(MCPXAPUState *d){(void)d;mcpx_apu_monitor_push_silence();}
static unsigned float_conversions;
static void vp_state_from_float_bits(MCPXAPUState *d){(void)d;float_conversions++;}
static int64_t vp_pitch_from_double_bits(uint64_t bits){return (int64_t)bits;}
/* POST LOAD */
static int16_t sample_at(uint64_t index,int channel){return (int16_t)(1+(index*13+channel*101)%30000);}
static void post_load_tests(void){
 const unsigned lengths[]={0,1,32,64,96,320,2048};unsigned cases=0;
 for(int version=1;version<=4;version++)for(unsigned n=0;n<sizeof(lengths)/sizeof(*lengths);n++){
  MCPXAPUState d={0};d.saved_output.frames=lengths[n];int16_t before[4096];
  for(unsigned i=0;i<4096;i++)before[i]=d.saved_output.samples[i]=(int16_t)(i*37);
  assert(mcpx_apu_output_post_load(&d,version)==0&&d.saved_output.loaded);
  assert(d.saved_output.frames==lengths[n]&&!memcmp(before,d.saved_output.samples,sizeof(before)));cases++;
 }
 MCPXAPUState d={0};d.saved_output.frames=2049;
 assert(mcpx_apu_output_post_load(&d,4)==-EINVAL&&!d.saved_output.loaded);
 assert(float_conversions==7);
 printf("PASS: %u legacy/current queued-PCM restores unchanged; oversized state rejected\n",cases);
}
int main(void){
 MCPXAPUState d={0};Error *err=NULL;mcpx_apu_monitor_init(&d,&err);assert(!err);
 uint64_t pulls=0;
 for(int mode=0;mode<3;mode++)for(unsigned ep_phase=0;ep_phase<8;ep_phase++)
 for(unsigned phase=0;phase<40000;phase+=1250){
  libretro_audio_discard();uint64_t made=0,taken=0,rem=0;
  for(unsigned video=1;video<=200;video++){
   uint64_t end=video*1001000ULL;
   uint64_t due=end>phase?(end-phase-1)/40000+1:0;
   while(made<due){
    if(mode==2){
     if(made%8==ep_phase){
      int16_t *out=mcpx_apu_monitor_begin_ahead(256);assert(out);
      for(int i=0;i<256;i++)for(int c=0;c<2;c++)out[2*i+c]=sample_at(made*32+i,c);
      mcpx_apu_monitor_end_ahead(out,256);
     }else mcpx_apu_monitor_push_silence();
    }else if(mode==1){
     int64_t mix[32][2];
     for(int i=0;i<32;i++)for(int c=0;c<2;c++)mix[i][c]=(int64_t)sample_at(made*32+i,c)*((int64_t)1<<29);
     mcpx_apu_monitor_push_mix((const int64_t (*)[2])mix,32);
    }else{
     int16_t *out=mcpx_apu_monitor_begin(32);assert(out);
     for(int i=0;i<32;i++)for(int c=0;c<2;c++)out[2*i+c]=sample_at(made*32+i,c);
     mcpx_apu_monitor_end(32);
    }
    made++;
   }
   if(video==73||video==137){
    int16_t saved[4096];uint32_t ahead;
    size_t count=mcpx_apu_monitor_save(saved,2048,&ahead);
    mcpx_apu_monitor_restore(saved,count,ahead);
   }
   rem+=4004;size_t want=rem/5;rem%=5;const int16_t *in=NULL;
   size_t available=libretro_audio_read_begin(&in);assert(available>=want+32);
   for(size_t i=0;i<want;i++)for(int c=0;c<2;c++){
    uint64_t pos=taken+i;int16_t expected=0;
    if(pos>=MCPX_APU_MONITOR_PREROLL_FRAMES*32){
     pos-=MCPX_APU_MONITOR_PREROLL_FRAMES*32;
     if(mode!=2||pos>=ep_phase*32)expected=sample_at(pos,c);
    }
    assert(in[2*i+c]==expected);
   }
   libretro_audio_read_end(want);taken+=want;pulls++;
  }
 }
 post_load_tests();mcpx_apu_monitor_finalize(&d);
 printf("PASS: %llu production queue pulls, sampled tick phases/all burst offsets; exact consecutive PCM, margin, reset and save/restore\n",(unsigned long long)pulls);
}

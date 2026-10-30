/* SPDX-License-Identifier: GPL-2.0-or-later
 * Production decoder, masks, notifications, fetcher, kernel, filters, voice
 * processing and voice-list gate. Only RAM/IRQ transport, worker dispatch,
 * debug controls and platform glue are fixtures. */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/mcpx/apu/apu_regs.h"
#include "hw/xbox/mcpx/apu/sample.h"
#include "hw/xbox/mcpx/apu/vp/adpcm.h"
#include "hw/xbox/mcpx/apu/vp/svf.h"
#include "hw/xbox/mcpx/apu/vp/hrtf.h"
#include "hw/xbox/mcpx/apu/vp/vpmath.h"
typedef uint64_t hwaddr;
typedef struct MCPXAPUState MCPXAPUState;
/* TYPES */
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DPRINTF(...) ((void)0)
#define ctz32(v) __builtin_ctz(v)
#define qatomic_or(ptr,v) (*(ptr)|=(v))
#define TARGET_PAGE_SIZE 4096
#define MCPX_APU_DEBUG_MON_VP 1
#define RAM_SIZE (1u<<20)
static size_t direct_ram_size=RAM_SIZE;
#define memory_region_size(region) direct_ram_size
static struct {struct {bool hrtf;}audio;}g_config;
struct MCPXAPUState {
    uint32_t regs[0x20000];
    uint8_t *ram_ptr;
    int set_irq;
    struct {
        MCPXAPUVoiceFilter filters[256];MCPXAPUVPSSLData ssl[256];
        uint8_t hrtf_submix[4],submix_headroom[32];
        int ssl_base_page;
        uint8_t hrtf_headroom;
        uint64_t voice_locked[4];
        int64_t sample_buf[32][2];
        struct {struct {int32_t hrir[2][31];int32_t itd;}entries[HRTF_ENTRY_COUNT];}hrtf;
    }vp;
    struct {int point;}monitor;
    unsigned notices[2],queued,idle;
    uint16_t queue[256];
};
static struct {MCPXAPUState *d;}address_space_memory;
/* the APU's register file, word-atomic in the core (apu_int.h) */
static uint32_t apu_reg_r(MCPXAPUState *d,unsigned r){return d->regs[r];}
static void apu_reg_w(MCPXAPUState *d,unsigned r,uint32_t v){d->regs[r]=v;}
#define retro_atomic_fetch_or_int(p,v) (*(p)|=(v))
#define retro_atomic_store_relaxed_int(p,v) (*(p)=(v))
static void cpu_physical_memory_read(hwaddr a,void *out,hwaddr bytes)
{assert(a+bytes<=RAM_SIZE);memcpy(out,address_space_memory.d->ram_ptr+a,bytes);}
static unsigned sge_reads;
static uint32_t apu_ram_ldl(MCPXAPUState *d,hwaddr a)
{
    assert(a+4<=RAM_SIZE);
    if(a>=0x4000 && a<0x4080)sge_reads++;
    return (uint32_t)d->ram_ptr[a]|((uint32_t)d->ram_ptr[a+1]<<8)|
        ((uint32_t)d->ram_ptr[a+2]<<16)|((uint32_t)d->ram_ptr[a+3]<<24);
}
static uint16_t apu_ram_lduw(MCPXAPUState *d,hwaddr a)
{assert(a+2<=RAM_SIZE);return d->ram_ptr[a]|(d->ram_ptr[a+1]<<8);}
static uint8_t apu_ram_ldub(MCPXAPUState *d,hwaddr a)
{assert(a<RAM_SIZE);return d->ram_ptr[a];}
static void store(MCPXAPUState *d,hwaddr a,uint32_t value)
{assert(a+4<=RAM_SIZE);for(int k=0;k<4;k++)d->ram_ptr[a+k]=value>>(8*k);}
#define stl_le_phys(space,a,v) store((space)->d,a,v)
#define ldl_le_phys(space,a) apu_ram_ldl((space)->d,a)
static void stb_phys(void *space,hwaddr a,uint8_t value)
{
    MCPXAPUState *d=address_space_memory.d;
    assert(a<RAM_SIZE);d->ram_ptr[a]=value;
    for(unsigned n=0;n<2;n++)
        if(a==d->regs[NV_PAPU_FENADDR]+16*(MCPX_HW_NOTIFIER_BASE_OFFSET+64*MCPX_HW_NOTIFIER_COUNT+MCPX_HW_NOTIFIER_SSLA_DONE+n)+15)
            d->notices[n]++;
}
static bool xemu_doge_mode(void){return false;}
static uint64_t fast_hash(const void *p,size_t n){return 0;}
static const uint8_t segaboot_jingle_data[2]={0};
static const size_t segaboot_jingle_size=0;
static int peek_ahead_multipass_bin(MCPXAPUState*d,uint16_t v,uint16_t*dest){*dest=0xffff;return -1;}
static bool allow_multipass;
static void get_multipass_samples(MCPXAPUState*d,int64_t mix[][32],uint16_t v,int32_t out[][2])
{assert(allow_multipass);memset(out,0,sizeof(int32_t[32][2]));}
/* INSERT PRODUCTION FUNCTIONS */

static const struct {hwaddr top,current,next;}voice_list_regs[]={
    {NV_PAPU_TVL2D,NV_PAPU_CVL2D,NV_PAPU_NVL2D},
    {NV_PAPU_TVL3D,NV_PAPU_CVL3D,NV_PAPU_NVL3D},
    {NV_PAPU_TVLMP,NV_PAPU_CVLMP,NV_PAPU_NVLMP}
};
static bool is_voice_locked(MCPXAPUState*d,uint16_t v){return false;}
static void fe_method(MCPXAPUState*d,uint32_t method,uint32_t arg){assert(method==SE2FE_IDLE_VOICE);d->idle++;}
static void voice_work_enqueue(MCPXAPUState*d,int v,int list){assert(d->queued<256);d->queue[d->queued++]=v;}
static void voice_work_dispatch(MCPXAPUState*d,int64_t mix[][32])
{for(unsigned i=0;i<d->queued;i++)voice_process(d,mix,d->vp.sample_buf,d->queue[i],0);d->queued=0;}
static int64_t monitor_output[32][2];
static void mcpx_apu_monitor_push_mix(const int64_t in[][2],int n){assert(n==32);memcpy(monitor_output,in,sizeof(monitor_output));}
/* VOICE FRAME */

static void put(MCPXAPUState*d,uint32_t reg,uint32_t mask,uint32_t value){voice_set_mask(d,64,reg,mask,value);}
static bool active(MCPXAPUState*d){return voice_get_mask(d,64,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);}
/* The test source's samples, in the VP's sample steps (2^23 full scale) */
static int32_t pcm_value(unsigned i,unsigned ch,unsigned format)
{
    if(format==0)return ((int)(i%33)-16+(ch?8:0))*65536;
    if(format==1)return (8192+(int)(i%13)*37-(ch?16384:0))*256;
    if(format==2)return 2097152+(int)(i%13)*9472-(ch?4194304:0);
    if(format==3)return (536870912+(int)(i%13)*2424832-(ch?1073741824:0))/256;
    return i<64?(ch?-2097152:2097152):(ch?-4194304:4194304);
}
static void fill(MCPXAPUState*d,unsigned address,unsigned n,unsigned format,bool stereo)
{
    unsigned channels=stereo?2:1;
    if(format==4){
        for(unsigned block=0;block<(n+63)/64;block++)for(unsigned ch=0;ch<channels;ch++)
            store(d,address+block*36*channels+ch*4,(uint16_t)(int16_t)((block?16384:8192)*(ch?-1:1)));
    }else for(unsigned i=0;i<n;i++)for(unsigned ch=0;ch<channels;ch++){
        unsigned size=format==0?1:format==1?2:4;
        int32_t x=pcm_value(i,ch,format);
        uint32_t value=format==0?x/65536+128:format==1?(uint16_t)(int16_t)(x/256):
            format==2?(uint32_t)x:(uint32_t)x*256u;
        hwaddr a=address+(i*channels+ch)*size;
        for(unsigned b=0;b<size;b++)d->ram_ptr[a+b]=value>>(8*b);
    }
}
static MCPXAPUState *setup(unsigned n,unsigned format,bool stereo,int pitch)
{
    MCPXAPUState*d=calloc(1,sizeof(*d));assert(d);
    d->ram_ptr=calloc(1,RAM_SIZE);assert(d->ram_ptr);address_space_memory.d=d;
    d->regs[NV_PAPU_VPVADDR]=0x1000;d->regs[NV_PAPU_VPSGEADDR]=0x4000;
    d->regs[NV_PAPU_VPSSLADDR]=0x8000;d->regs[NV_PAPU_FENADDR]=0x80000;
    for(unsigned page=0;page<16;page++)store(d,0x4000+page*8,0x20000+page*4096);
    d->regs[NV_PAPU_TVL2D]=64;d->regs[NV_PAPU_TVL3D]=d->regs[NV_PAPU_TVLMP]=0xffff;
    put(d,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE,1);
    put(d,NV_PAVS_VOICE_TAR_PITCH_LINK,NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,0xffff);
    put(d,NV_PAVS_VOICE_TAR_PITCH_LINK,NV_PAVS_VOICE_TAR_PITCH_LINK_PITCH,(uint16_t)pitch);
    put(d,NV_PAVS_VOICE_PAR_NEXT,NV_PAVS_VOICE_PAR_NEXT_EBO,n-1);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_STEREO,stereo);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK,stereo);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE,format<4?format:2);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,format==0?0:format==1?1:format==4?2:3);
    static const uint32_t masks[]={NV_PAVS_VOICE_CFG_VBIN_V0BIN,NV_PAVS_VOICE_CFG_VBIN_V1BIN,
        NV_PAVS_VOICE_CFG_VBIN_V2BIN,NV_PAVS_VOICE_CFG_VBIN_V3BIN,NV_PAVS_VOICE_CFG_VBIN_V4BIN,NV_PAVS_VOICE_CFG_VBIN_V5BIN};
    for(unsigned b=0;b<6;b++)put(d,NV_PAVS_VOICE_CFG_VBIN,masks[b],b);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_V6BIN,6);put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_V7BIN,7);
    voice_reset_filters(d,64);
    d->vp.filters[64].gains=true;for(unsigned b=0;b<8;b++)d->vp.filters[64].route_gain[b]=16384;
    fill(d,0x20000,n,format,stereo);
    return d;
}
static void dispose(MCPXAPUState*d){free(d->ram_ptr);free(d);}
static void frame(MCPXAPUState*d,int64_t mix[32][32]){memset(mix,0,sizeof(int64_t[32][32]));mcpx_apu_vp_frame(d,mix);}
static void inclusive_boundary(void)
{
    MCPXAPUState*d=setup(33,1,false,0);int32_t out[32][2];
    assert(voice_get_samples(d,64,out,32)==32);
    /* Reading 0..31 must not complete a buffer whose inclusive end is 32. */
    assert(active(d));assert(d->notices[0]==0);
    assert(voice_get_samples(d,64,out,1)==1);
    assert(out[0][0]==pcm_value(32,0,1));
    dispose(d);puts("PASS: inclusive EBO=32 retains the 33rd sample");
}
/* The resampled sample in a mix bin at unity gain and headroom 0: the
 * kernel's weights in 1/2^24, the sum rounded half up to a sample step,
 * then 2^21 mix steps a sample step (sample.h). Integer positions are the
 * source sample itself. */
static int64_t reference(unsigned n,unsigned format,bool stereo,uint64_t pos,unsigned channel,bool loop,unsigned lbo)
{
    unsigned fraction=pos&0xffff,row=fraction>>8,a=fraction&255;
    int64_t sum=0;
    for(int k=0;k<8;k++){
        int64_t at=(int64_t)(pos>>16)-3+k;
        if(at<0)continue;
        if((uint64_t)at>=n){if(!loop)continue;at=lbo+(at-n)%(n-lbo);}
        int64_t w=fraction?lagrange_rows[row][k]*256+(lagrange_rows[row+1][k]-lagrange_rows[row][k])*(int64_t)a:
            k==3?(int64_t)1<<24:0;
        sum+=w*pcm_value(at,stereo?channel:0,format);
    }
    return ((sum+(1<<23))>>24) * ((int64_t)1 << VP_MIX_SHIFT);
}
static void tails(void)
{
    static const unsigned lengths[]={1,2,3,4,7,31,32,33,63,64,65,127};
    static const int pitches[]={-8192,-4096,0,4096};
    unsigned cases=0;
    for(unsigned format=0;format<5;format++)for(unsigned li=0;li<ARRAY_SIZE(lengths);li++)
    for(unsigned pi=0;pi<ARRAY_SIZE(pitches);pi++)for(unsigned stereo=0;stereo<2;stereo++)for(unsigned mon=0;mon<2;mon++){
        unsigned n=lengths[li];if(format==4 && n>65)continue;
        MCPXAPUState*d=setup(n,format,stereo,pitches[pi]);d->monitor.point=mon?MCPX_APU_DEBUG_MON_VP:0;
        uint64_t pos=0;uint32_t step=exp2(pitches[pi]/4096.0)*65536;
        unsigned frames=0;bool ended=false;
        while(!ended){
            assert(frames++<100);int64_t mix[32][32];frame(d,mix);
            for(unsigned i=0;i<32;i++){
                for(unsigned ch=0;ch<2;ch++){
                    int64_t expected=reference(n,format,stereo,pos,ch,false,0);
                    int64_t got=mon?monitor_output[i][ch]:mix[ch][i];
                    if(got!=expected){fprintf(stderr,"tail lost: format=%u n=%u pitch=%d stereo=%u mon=%u frame=%u sample=%u got=%lld expected=%lld\n",format,n,pitches[pi],stereo,mon,frames,i,(long long)got,(long long)expected);abort();}
                }
                if((pos>>16)>=n+3)ended=true;
                pos+=step;
            }
            assert(active(d)==!ended);
            assert(d->notices[0]==(unsigned)ended);
        }
        int64_t mix[32][32];frame(d,mix);assert(d->notices[0]==1);
        for(unsigned i=0;i<32;i++)assert((mon?monitor_output[i][0]:mix[0][i])==0);
        dispose(d);cases++;
    }
    printf("PASS: %u real PCM/ADPCM decoder + resampler + active-list tail cases, pitches/channels/DSP-off output and completion exactly once\n",cases);
}
static void loops(void)
{
    for(unsigned n=1;n<=33;n+=16){
        unsigned lbo=n>1?1:0;
        MCPXAPUState*d=setup(n,1,true,-4096);
        put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_LOOP,1);
        put(d,NV_PAVS_VOICE_CUR_PSH_SAMPLE,NV_PAVS_VOICE_CUR_PSH_SAMPLE_LBO,lbo);
        uint64_t pos=0;
        for(unsigned f=0;f<16;f++){
            int64_t mix[32][32];frame(d,mix);
            for(unsigned i=0;i<32;i++){
                assert(mix[0][i]==reference(n,1,true,pos,0,true,lbo));
                pos+=32768;
            }
            assert(active(d)&&d->notices[0]==0);
        }
        dispose(d);
    }
    puts("PASS: real one/17/33-sample looping buffers and nonzero loop points without skips or completion");
}
static void streams(void)
{
    MCPXAPUState*d=setup(33,1,false,0);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_DATA_TYPE,1);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_PERSIST,1);
    d->vp.ssl[64].base[0]=0;d->vp.ssl[64].base[1]=1;
    d->vp.ssl[64].count[0]=d->vp.ssl[64].count[1]=1;
    store(d,0x8000,0x20000);store(d,0x8004,33|(1u<<16));
    store(d,0x8008,0x22000);store(d,0x800c,33|(1u<<16));fill(d,0x22000,33,1,false);
    MCPXAPUVoiceFilter*f=&d->vp.filters[64];f->voice=64;
    voice_resample_fetch(d,f);
    assert(d->notices[0]==0&&d->vp.ssl[64].ssl_index==0);
    voice_resample_fetch(d,f);
    assert(f->resample_buf[0][0]==pcm_value(32,0,1));
    assert(f->resample_buf[1][0]==pcm_value(0,0,1));assert(d->notices[0]==1);
    d->vp.ssl[64].count[0]=0;
    voice_resample_fetch(d,f);
    assert(f->resample_buf[0][0]==pcm_value(31,0,1));assert(f->resample_buf[1][0]==pcm_value(32,0,1));
    for(unsigned i=2;i<32;i++)assert(f->resample_buf[i][0]==0);
    assert(active(d)&&d->notices[1]==1);
    d->vp.ssl[64].count[1]=0;
    voice_resample_fetch(d,f);for(unsigned i=0;i<32;i++)assert(f->resample_buf[i][0]==0);
    d->vp.ssl[64].count[0]=1;store(d,0x8004,5|(1u<<16));
    voice_resample_fetch(d,f);
    for(unsigned i=0;i<5;i++)assert(f->resample_buf[i][0]==pcm_value(i,0,1));
    for(unsigned i=5;i<32;i++)assert(f->resample_buf[i][0]==0);
    assert(active(d));dispose(d);
    puts("PASS: real SSL 33-sample bank boundary, last samples, completion bank, persistent underrun silence and refill");
}
static void control_paths(void)
{
    MCPXAPUState*d=setup(33,1,false,-8192);int64_t mix[32][32];
    frame(d,mix);assert(active(d)&&d->notices[0]==0);
    uint32_t phase=d->vp.filters[64].resample_phase;
    put(d,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_PAUSED,1);
    frame(d,mix);assert(active(d)&&d->notices[0]==0);
    assert(d->vp.filters[64].resample_phase==phase);
    put(d,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_PAUSED,0);
    voice_off(d,64);frame(d,mix);
    assert(!active(d)&&d->notices[0]==1);
    for(unsigned i=0;i<32;i++)assert(mix[0][i]==0);
    /* Restart while stale EOF or buffered samples remain. */
    voice_reset_filters(d,64);
    put(d,NV_PAVS_VOICE_PAR_OFFSET,NV_PAVS_VOICE_PAR_OFFSET_CBO,0);
    put(d,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE,1);
    frame(d,mix);assert(active(d)&&d->notices[0]==1);
    assert(mix[0][1]!=0); /* First sample fades from zero; following ones play. */
    mcpx_apu_vp_reset(d);
    assert(d->vp.filters[64].resample_len==0);
    /* Simulate a pre-load restoring an active voice and its CBO. */
    put(d,NV_PAVS_VOICE_PAR_OFFSET,NV_PAVS_VOICE_PAR_OFFSET_CBO,0);
    frame(d,mix);assert(active(d));
    dispose(d);
    /* Finite SSL source in release drains rather than stopping on prefetch. */
    d=setup(33,1,false,0);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_DATA_TYPE,1);
    put(d,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_EACUR,NV_PAVS_VOICE_PAR_STATE_EFCUR_RELEASE);
    put(d,NV_PAVS_VOICE_TAR_LFO_ENV,NV_PAVS_VOICE_TAR_LFO_ENV_EA_RELEASERATE,4095);
    put(d,NV_PAVS_VOICE_PAR_OFFSET,NV_PAVS_VOICE_PAR_OFFSET_EALVL,255);
    d->vp.ssl[64].count[0]=1;
    store(d,0x8000,0x20000);store(d,0x8004,33|(1u<<16));
    frame(d,mix);assert(active(d)&&d->notices[0]==1);
    frame(d,mix);assert(!active(d)&&d->notices[0]==2);
    frame(d,mix);assert(d->notices[0]==2);dispose(d);
    d=setup(33,1,false,-32768);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_DATA_TYPE,1);
    put(d,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_EACUR,NV_PAVS_VOICE_PAR_STATE_EFCUR_RELEASE);
    put(d,NV_PAVS_VOICE_TAR_LFO_ENV,NV_PAVS_VOICE_TAR_LFO_ENV_EA_RELEASERATE,4095);
    put(d,NV_PAVS_VOICE_PAR_OFFSET,NV_PAVS_VOICE_PAR_OFFSET_EALVL,255);
    frame(d,mix);assert(!active(d)&&d->notices[0]==1);dispose(d);
    /* A malformed loop that cannot provide a sample must not spin fetch. */
    d=setup(1,1,false,0);
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_LOOP,1);
    put(d,NV_PAVS_VOICE_CUR_PSH_SAMPLE,NV_PAVS_VOICE_CUR_PSH_SAMPLE_LBO,2);
    put(d,NV_PAVS_VOICE_PAR_OFFSET,NV_PAVS_VOICE_PAR_OFFSET_CBO,2);
    frame(d,mix);assert(active(d));dispose(d);
    puts("PASS: pause, immediate explicit stop, restart, VP reset/pre-load, finite SSL drain and zero-progress guard");
}
static void adpcm_pages(void)
{
    unsigned cases=0;
    for(unsigned stereo=0;stereo<2;stereo++)for(unsigned stream=0;stream<2;stream++)
    for(unsigned fallback=0;fallback<(stream?1:2);fallback++)
    for(unsigned offset=0;offset<4096;offset++){
        direct_ram_size=fallback?0:RAM_SIZE;
        MCPXAPUState*d=setup(128,4,stereo,0);
        unsigned channels=stereo?2:1, bytes=36*channels;
        uint8_t encoded[144];int16_t expected[130];
        for(unsigned block=0;block<2;block++){
            for(unsigned i=0;i<bytes;i++)encoded[block*bytes+i]=(i*73+offset*17+block*31)&255;
            for(unsigned ch=0;ch<channels;ch++){
                encoded[block*bytes+ch*4+2]=(offset+ch*29+block*13)%89;
                encoded[block*bytes+ch*4+3]=0;
            }
        }
        /* Deliberately discontiguous SGE pages, including every possible
         * split within the predictor header and nibble payload. */
        store(d,0x4008,0x60000);
        put(d,NV_PAVS_VOICE_CUR_PSL_START,NV_PAVS_VOICE_CUR_PSL_START_BA,offset);
        for(unsigned i=0;i<2*bytes;i++){
            unsigned at=offset+i;
            d->ram_ptr[stream?0x70000+at:at<4096?0x20000+at:0x60000+at-4096]=encoded[i];
        }
        if(stream){
            put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_DATA_TYPE,1);
            put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_PERSIST,1);
            d->vp.ssl[64].count[0]=1;
            store(d,0x8000,0x70000+offset);
            store(d,0x8004,128|(2u<<16)|(stereo<<18)|(stereo<<23));
        }
        for(unsigned block=0;block<2;block++){
            assert(adpcm_decode_block(expected,encoded+block*bytes,bytes,channels)==65);
            int32_t out[64][2];sge_reads=0;
            assert(voice_get_samples(d,64,out,64)==64);
            unsigned at=offset+block*bytes;
            assert(sge_reads==(stream?0:1+(at%4096+bytes>4096)));
            for(unsigned i=0;i<64;i++)for(unsigned ch=0;ch<2;ch++)
                assert(out[i][ch]==vp_sample_s16(expected[i*channels+(stereo?ch:0)]));
        }
        dispose(d);cases++;
    }
    direct_ram_size=RAM_SIZE;
    printf("PASS: %u mono/stereo ADPCM buffer/SSL alignments, discontiguous SGE splits, address-space fallback and exact translation counts\n",cases);
}
static void pitch_cache(void)
{
    MCPXAPUState*d=setup(128,1,false,0);
    MCPXAPUVoiceFilter*f=&d->vp.filters[64];
    /* pitches in 1/512 of the register's unit: 0, -4096, 1700.125, ... */
    static const int64_t targets[]={0,-4096*512,870464,-32768*512,32767*512,0};
    int64_t previous=0;
    for(unsigned t=0;t<ARRAY_SIZE(targets);t++){
        int64_t target=targets[t];
        for(unsigned repeat=0;repeat<8;repeat++){
            int64_t from=t==0||repeat?target:previous;
            VoicePitchRamp ramp=voice_pitch_ramp(f,target);
            for(unsigned n=0;n<32;n++)
                assert(voice_pitch_step(&ramp,n)==(uint32_t)(exp2((from*32+(target-from)*(int64_t)n)/16384.0/4096)*65536));
        }
        previous=target;
    }
    /* A multipass frame updates the log-pitch target without consuming
     * source samples. Returning to source playback must use that target. */
    allow_multipass=true;
    put(d,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_MULTIPASS,1);
    put(d,NV_PAVS_VOICE_TAR_PITCH_LINK,NV_PAVS_VOICE_TAR_PITCH_LINK_PITCH,4096);
    int64_t mix[32][32]={0};voice_process(d,mix,d->vp.sample_buf,64,0);
    VoicePitchRamp ramp=voice_pitch_ramp(f,4096*512);
    assert(ramp.from==4096*512&&ramp.fixed_step==131072);
    allow_multipass=false;
    voice_reset_filters(d,64);
    ramp=voice_pitch_ramp(f,-4096*512);
    assert(ramp.from==-4096*512&&ramp.fixed_step==32768);
    dispose(d);
    puts("PASS: steady/changing fractional and extreme pitch steps, multipass-to-source transition and reset cache invalidation");
}
/* An ADPCM block whose header the decoder rejects plays silence, the same
 * every run, whatever the stack held */
static void rejected_adpcm(void)
{
    for(unsigned stereo=0;stereo<2;stereo++)for(unsigned bad=0;bad<2;bad++){
        MCPXAPUState*d=setup(64,4,stereo,0);
        /* step index 89, or the reserved byte, of the last channel's header */
        unsigned at=0x20000+(stereo?4:0);
        if(bad)d->ram_ptr[at+3]=1;else d->ram_ptr[at+2]=89;
        int64_t mix[32][32];
        for(unsigned f=0;f<3;f++){
            memset(mix,0x5a,sizeof(mix));frame(d,mix);
            for(unsigned b=0;b<2;b++)for(unsigned i=0;i<32;i++)assert(mix[b][i]==0);
        }
        dispose(d);
    }
    puts("PASS: rejected ADPCM headers play silence");
}
static uint64_t filter_hash(uint64_t hash, int64_t sample)
{
    uint64_t bits = sample;
    for (unsigned i = 0; i < 8; i++) {
        hash = (hash ^ (bits & 255)) * 1099511628211ULL;
        bits >>= 8;
    }
    return hash;
}
static void signed_filters(void)
{
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned cutoff = 0; cutoff < 2; cutoff++)
    for (unsigned q = 0; q < 8; q++) {
        MCPXAPUState *d = setup(1024, 1, true, 0);
        put(d, NV_PAVS_VOICE_CFG_MISC, NV_PAVS_VOICE_CFG_MISC_FMODE, 2);
        put(d, NV_PAVS_VOICE_CFG_MISC, NV_PAVS_VOICE_CFG_MISC_PARAEQ_Q, q);
        uint32_t fc = (uint16_t)(int16_t)(cutoff ? -4096 : 0);
        put(d, NV_PAVS_VOICE_TAR_FCA, UINT32_MAX, fc | (8192u << 16));
        put(d, NV_PAVS_VOICE_TAR_FCB, UINT32_MAX, fc | (57344u << 16));
        for (unsigned pass = 0; pass < 2; pass++) {
            int64_t mix[32][32];
            frame(d, mix);
            assert(d->vp.filters[64].peq_set);
            for (unsigned b = 0; b < 32; b++)
            for (unsigned i = 0; i < 32; i++) hash = filter_hash(hash, mix[b][i]);
            for (unsigned ch = 0; ch < 2; ch++) {
                const sv_filter *s = &d->vp.filters[64].peq[ch];
                hash = filter_hash(hash, s->h);
                hash = filter_hash(hash, s->b);
                hash = filter_hash(hash, s->l);
            }
        }
        dispose(d);
    }
    const unsigned coefficients[] = { 0, 1, 32768, 65535 };
    for (unsigned c = 0; c < 4; c++) {
        MCPXAPUState *d = setup(1024, 1, false, 0);
        /* Move the configured voice to the 3D range and use negative PCM. */
        memcpy(d->ram_ptr + 0x1000,
               d->ram_ptr + 0x1000 + 64 * NV_PAVS_SIZE, NV_PAVS_SIZE);
        for (unsigned i = 0; i < 1024; i++) {
            uint16_t sample = (uint16_t)(int16_t)(-8192 - (int)(i % 13) * 37);
            d->ram_ptr[0x20000 + i * 2] = sample;
            d->ram_ptr[0x20001 + i * 2] = sample >> 8;
        }
        voice_reset_filters(d, 0);
        voice_set_mask(d, 0, NV_PAVS_VOICE_CFG_HRTF_TARGET,
                       NV_PAVS_VOICE_CFG_HRTF_TARGET_HANDLE, 0);
        voice_set_mask(d, 0, NV_PAVS_VOICE_CFG_MISC,
                       NV_PAVS_VOICE_CFG_MISC_FMODE, 3);
        voice_set_mask(d, 0, NV_PAVS_VOICE_TAR_FCB, UINT32_MAX,
                       coefficients[c] | (coefficients[3-c] << 16));
        for (unsigned pass = 0; pass < 2; pass++) {
            int64_t mix[32][32] = {{0}}, monitor[32][2] = {{0}};
            voice_process(d, mix, monitor, 0, 0);
            assert(d->vp.filters[0].i3dl2[0] <= 0 &&
                   d->vp.filters[0].i3dl2[1] <= 0);
            assert(d->vp.filters[0].i3dl2[0] || d->vp.filters[0].i3dl2[1]);
            for (unsigned b = 0; b < 32; b++)
            for (unsigned i = 0; i < 32; i++) hash = filter_hash(hash, mix[b][i]);
            hash = filter_hash(hash, d->vp.filters[0].i3dl2[0]);
            hash = filter_hash(hash, d->vp.filters[0].i3dl2[1]);
        }
        dispose(d);
    }
    /* Captured before the scaling change with production signed-wrap flags.
     * Canonical little-endian integer hashing makes this host-independent. */
    assert(hash == UINT64_C(0x2482e118ff7aabf8));
    printf("PASS: ParaEQ/spatial signed filter histories, hash=%016llx\n",
           (unsigned long long)hash);
}
int main(int argc,char**argv)
{
    if(argc>1&&strcmp(argv[1],"tail")==0){tails();return 0;}
    if(argc>1&&strcmp(argv[1],"filters")==0){signed_filters();return 0;}
    signed_filters();
    inclusive_boundary();tails();loops();streams();control_paths();adpcm_pages();rejected_adpcm();pitch_cache();return 0;
}

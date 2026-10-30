/* SPDX-License-Identifier: GPL-2.0-or-later
 * The voice processor's frame on four threads (voice_work_dispatch: units
 * claimed by the emulator thread and three workers) against the same frame
 * on one, for random voices: every format, pitch ramps, envelopes, LFOs,
 * filters, HRTF, the VP monitor, multipass groups and multipass voices
 * mixing into the bin they read. Odd scenarios use DirectSound's layout
 * (3D voices into the multipass bin), so their frames split into units.
 * Built by test-vp-threads.py around vp.c's own functions and the fixture
 * of test-voice-tail.c. */

static const struct {hwaddr top,current,next;}voice_list_regs[]={
    {NV_PAPU_TVL2D,NV_PAPU_CVL2D,NV_PAPU_NVL2D},
    {NV_PAPU_TVL3D,NV_PAPU_CVL3D,NV_PAPU_NVL3D},
    {NV_PAPU_TVLMP,NV_PAPU_CVLMP,NV_PAPU_NVLMP}
};
static bool is_voice_locked(MCPXAPUState*d,uint16_t v){return false;}
static void fe_method(MCPXAPUState*d,uint32_t method,uint32_t arg){d->idle++;}

typedef int64_t mix_t;
static int cur_pass;
static int16_t mon_out[32][2];
static void mcpx_apu_monitor_push_mix(const int64_t in[][2],int n)
{for(int i=0;i<32;i++)for(int c=0;c<2;c++)mon_out[i][c]=mcpx_apu_monitor_convert_mix(in[i][c],65536);}
static int to24(int64_t x){return vp_mix_to_24(x);}

/* VOICE FRAME */

static uint64_t rs;
static uint32_t rnd(void){rs^=rs<<13;rs^=rs>>7;rs^=rs<<17;return (uint32_t)(rs>>11);}
static uint32_t rr(uint32_t n){return rnd()%n;}
static void vset(MCPXAPUState*d,int v,uint32_t reg,uint32_t mask,uint32_t val){voice_set_mask(d,v,reg,mask,val);}

int main(int argc,char**argv)
{
    int scenarios=argc>1?atoi(argv[1]):400;
    long compared=0,parallel_frames=0;
    for(int sc=0;sc<scenarios;sc++)for(int pass=0;pass<2;pass++){
        rs=0x9e3779b97f4a7c15ull^((uint64_t)sc*0x100000001b3ull);rnd();rnd();
        MCPXAPUState*d=calloc(1,sizeof(*d));
        cur_pass=pass; g_config.audio.vp.num_workers=pass?4:1;
        voice_work_init(d);
        d->ram_ptr=calloc(1,RAM_SIZE);address_space_memory.d=d;
        d->regs[NV_PAPU_VPVADDR]=0x1000;d->regs[NV_PAPU_VPSGEADDR]=0x10000;
        d->regs[NV_PAPU_VPSSLADDR]=0x18000;d->regs[NV_PAPU_FENADDR]=0x80000;
        for(unsigned page=0;page<16;page++)store(d,0x10000+page*8,0x20000+page*4096);
        for(unsigned a=0x20000;a<0x30000;a++)d->ram_ptr[a]=rnd();
        g_config.audio.hrtf=rr(4)!=0;
        d->monitor.point=rr(2)?MCPX_APU_DEBUG_MON_VP:0;
        for(int b=0;b<32;b++)d->vp.submix_headroom[b]=rr(3)?rr(8):0;
        for(int b=0;b<4;b++)d->vp.hrtf_submix[b]=rr(31);
        for(int e=0;e<4;e++){
            for(int c=0;c<2;c++)for(int k=0;k<31;k++)d->vp.hrtf.entries[e].hrir[c][k]=hrir_tap((uint8_t)rnd());
            int16_t itd=(int16_t)(rr(2*42*512+600)-(42*512+300));
            d->vp.hrtf.entries[e].itd=itd;
        }
        int nv=2+rr(30);
        int handles[64];int lists[64];
        int heads[3]={0xffff,0xffff,0xffff},tails[3]={-1,-1,-1};
        int mpbin=31; int style=sc&1; /* 0: no multipass bin; 1: 3D voices feed it */
        for(int i=0;i<nv;i++){
            int list=style&&rr(6)==0?2:rr(2);
            int v;
            do{v=list==1?rr(64):64+rr(192);bool dup=false;for(int j=0;j<i;j++)dup|=handles[j]==v;if(!dup)break;}while(1);
            handles[i]=v;lists[i]=list;
            for(int r=0;r<0x80;r+=4)vset(d,v,r,0xffffffff,0);
            vset(d,v,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE,1);
            vset(d,v,NV_PAVS_VOICE_TAR_PITCH_LINK,NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,0xffff);
            if(tails[list]<0)heads[list]=v;else vset(d,tails[list],NV_PAVS_VOICE_TAR_PITCH_LINK,NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,v);
            tails[list]=v;
            unsigned format=rr(5);bool stereo=rr(2);unsigned n=1+rr(400);if(getenv("DBG"))fprintf(stderr,"sc %d v %d list %d fmt %u st %d n %u\n",sc,v,list,format,stereo,n);
            vset(d,v,NV_PAVS_VOICE_TAR_PITCH_LINK,NV_PAVS_VOICE_TAR_PITCH_LINK_PITCH,(uint16_t)(int16_t)(1?rr(20000)-14000:0));
            vset(d,v,NV_PAVS_VOICE_PAR_NEXT,NV_PAVS_VOICE_PAR_NEXT_EBO,n-1);
            if(format==4)for(unsigned bk=0;bk<(n+63)/64+1;bk++)for(int c=0;c<(stereo?2:1);c++){
                unsigned a=0x20000+i*0x3000+bk*36*(stereo?2:1)+c*4;d->ram_ptr[a+2]=rr(89);d->ram_ptr[a+3]=0;}
            vset(d,v,NV_PAVS_VOICE_CUR_PSL_START,NV_PAVS_VOICE_CUR_PSL_START_BA,i*0x3000);
            if(list==2){
                vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_MULTIPASS,1);
                vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_MULTIPASS_BIN,mpbin);
                vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX,1);
                vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_HEADROOM,rr(8));
            }else{
                vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_STEREO,stereo);
                vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK,stereo);
                vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE,format<4?format:2);
                vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,format==0?0:format==1?1:format==4?2:3);
                if(rr(2)&&n>1){vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_LOOP,1);
                    vset(d,v,NV_PAVS_VOICE_CUR_PSH_SAMPLE,NV_PAVS_VOICE_CUR_PSH_SAMPLE_LBO,rr(n));}
            }
            vset(d,v,NV_PAVS_VOICE_CFG_VBIN,0xffffffff,0);for(int bb=0;bb<6;bb++)vset(d,v,NV_PAVS_VOICE_CFG_VBIN,0x1Fu<<(5*bb),rr(31));
            if(style&&list==1)vset(d,v,NV_PAVS_VOICE_CFG_VBIN,NV_PAVS_VOICE_CFG_VBIN_V4BIN,mpbin);
            vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_V6BIN,rr(31));
            vset(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_V7BIN,rr(31));
            uint32_t vol=rr(4)?rnd()&0x3ff3ff3f:rnd();
            vset(d,v,NV_PAVS_VOICE_TAR_VOLA,0xffffffff,1?vol:0);
            vol=rr(4)?rnd()&0x3ff3ff3f:rnd();vset(d,v,NV_PAVS_VOICE_TAR_VOLB,0xffffffff,1?vol:0);
            vol=rr(4)?rnd()&0x3ff3ff3f:rnd();vset(d,v,NV_PAVS_VOICE_TAR_VOLC,0xffffffff,1?vol:0);
            vset(d,v,NV_PAVS_VOICE_TAR_FCA,0xffffffff,rnd());
            vset(d,v,NV_PAVS_VOICE_TAR_FCB,0xffffffff,rnd());
            {uint32_t m=rnd();vset(d,v,NV_PAVS_VOICE_CFG_MISC,0xffffffff,1?m:m&~NV_PAVS_VOICE_CFG_MISC_FMODE);}
            {uint32_t m=rr(2)?rnd():0;vset(d,v,NV_PAVS_VOICE_TAR_LFO_MOD,0xffffffff,1?m:0);}
            vset(d,v,NV_PAVS_VOICE_TAR_LFO_ENV,0xffffffff,rnd());
            vset(d,v,NV_PAVS_VOICE_CFG_ENV0,0xffffffff,rnd());
            vset(d,v,NV_PAVS_VOICE_CFG_ENVA,0xffffffff,rnd());
            vset(d,v,NV_PAVS_VOICE_CFG_ENV1,0xffffffff,rnd());
            vset(d,v,NV_PAVS_VOICE_CFG_ENVF,0xffffffff,rnd());
            {uint32_t a=rr(4)?NV_PAVS_VOICE_PAR_STATE_EFCUR_OFF:rr(7),f=rr(7);
            vset(d,v,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_EACUR,1?a:0);
            vset(d,v,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_EFCUR,1?f:0);}
            vset(d,v,NV_PAVS_VOICE_PAR_OFFSET,NV_PAVS_VOICE_PAR_OFFSET_EALVL,rr(256));
            vset(d,v,NV_PAVS_VOICE_PAR_NEXT,NV_PAVS_VOICE_PAR_NEXT_EFLVL,rr(256));
            {uint32_t h=rr(5)?rr(4):0xffff;vset(d,v,NV_PAVS_VOICE_CFG_HRTF_TARGET,NV_PAVS_VOICE_CFG_HRTF_TARGET_HANDLE,1?h:0xffff);}
            voice_reset_filters(d,v);
        }
        d->regs[NV_PAPU_TVL2D]=heads[0];d->regs[NV_PAPU_TVL3D]=heads[1];d->regs[NV_PAPU_TVLMP]=heads[2];
        int frames=8+rr(24);
        for(int f=0;f<frames;f++){
            static mix_t mix[32][32];
            memset(mix,0,sizeof(mix));
            if(d->monitor.point)memset(mon_out,0,sizeof(mon_out));
            mcpx_apu_vp_frame(d,mix);
            static int out[2][64][32*32+64];
            for(int b=0;b<32;b++)for(int ii=0;ii<32;ii++)out[pass][f][b*32+ii]=to24(mix[b][ii]);
            for(int ii=0;ii<32;ii++){out[pass][f][1024+2*ii]=mon_out[ii][0];out[pass][f][1025+2*ii]=mon_out[ii][1];}
            if(pass){compared++;if(memcmp(out[0][f],out[1][f],sizeof(out[0][f]))){
                for(int x=0;x<1088;x++)if(out[0][f][x]!=out[1][f][x]){fprintf(stderr,"FAIL: scenario %d frame %d: %s %d sample %d serial %d threaded %d\n",sc,f,x<1024?"bin":"monitor",x<1024?x/32:(x-1024)/2,x<1024?x%32:x%2,out[0][f][x],out[1][f][x]);break;}
                exit(1);}}
        }
        if(pass&&d->vp.voice_work_dispatch.num_units)parallel_frames++;
        voice_work_finalize(d);
        free(d->ram_ptr);free(d);
    }
    printf("PASS: %ld frames of random voices (multipass groups, HRTF, filters, the monitor) the same on four voice threads as on one; %ld scenarios split\n",compared,parallel_frames);
    return 0;
}

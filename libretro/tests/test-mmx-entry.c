/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t fpstt;uint8_t fptags[8];uint64_t payload[16],fip;} CPUX86State;
typedef struct {bool mmx_entered,pending;unsigned flags,cpuid_7_0_ebx_features;void *A0;struct {unsigned is_jmp;} base;} DisasContext;
typedef struct {unsigned unused;} X86DecodedInsn;
typedef uint64_t *TCGv_i64;
enum {HF_EM_MASK=1,HF_TS_MASK=2,CPUID_7_0_EBX_MPX=4,DISAS_EOB_NEXT=7,R_EAX=0,R_EDX=1};
static CPUX86State machine,restore_state;
static void *tcg_env=&machine;
static uint32_t *fpstt=&machine.fpstt;
static uint64_t temp,constant;
static uint32_t cpu_regs[2];
static unsigned flushes,stores,exceptions,restore_calls;
static bool xemu_cpu_boost;
/* HELPERS */
static void gen_flush_fp(DisasContext *s) {flushes++;if(s->pending) {machine.payload[3]=0x1234;machine.fip=0x5678;s->pending=false;}}
static TCGv_i64 tcg_constant_i64(uint64_t v) {constant=v;return &constant;}
static void tcg_gen_movi_i32(uint32_t *d,uint32_t v) {*d=v;}
static void tcg_gen_st_i64(TCGv_i64 v,void *env,size_t offset) {memcpy((char *)env+offset,v,8);stores++;}
static void gen_helper_emms(void *env) {helper_emms(env);}
static void gen_NM_exception(DisasContext *s) {exceptions++;}
static void gen_helper_fxrstor(void *env,void *address) {memcpy(env,address,sizeof(machine));restore_calls++;}
static TCGv_i64 tcg_temp_new_i64(void) {return &temp;}
static void tcg_gen_concat_tl_i64(TCGv_i64 v,uint32_t lo,uint32_t hi) {*v=(uint64_t)hi<<32|lo;}
static void gen_helper_xrstor(void *env,void *address,TCGv_i64 features) {gen_helper_fxrstor(env,address);}
/* PRODUCTION */
static uint64_t rng=0x873536;
static uint64_t next(void) {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
int main(void) {
    unsigned cases=0;
    for(unsigned boost=0;boost<2;boost++) for(unsigned top=0;top<8;top++)
    for(unsigned tags=0;tags<256;tags++) for(unsigned transition=0;transition<5;transition++) {
        xemu_cpu_boost=boost;memset(&machine,0,sizeof(machine));machine.fpstt=top;
        for(unsigned i=0;i<8;i++) machine.fptags[i]=(tags>>i)&1;
        for(unsigned i=0;i<16;i++) machine.payload[i]=next();
        machine.fip=next();
        CPUX86State expected=machine;expected.payload[3]=0x1234;expected.fip=0x5678;
        DisasContext s={.pending=true,.A0=&restore_state};X86DecodedInsn decode={0};
        flushes=stores=exceptions=restore_calls=0;
        for(unsigned i=0;i<16;i++) {helper_enter_mmx(&expected);gen_enter_mmx(&s);assert(!memcmp(&machine,&expected,sizeof(machine)));}
        assert(flushes==1 && stores==1 && !s.pending);
        if(transition==0) {
            gen_EMMS(&s,&decode);helper_emms(&expected);
        } else if(transition==1 || transition==2) {
            restore_state=machine;restore_state.fpstt=top;memset(restore_state.fptags,1,8);expected=restore_state;
            if(transition==1) gen_FXRSTOR(&s,&decode);else gen_XRSTOR(&s,&decode);
            assert(restore_calls==1);
        } else {
            /* x87 escapes invalidate conservatively, including stack ops
             * and FNINIT/FLDENV/FRSTOR, before their state change. */
            s.mmx_entered=false;machine.fpstt=top;machine.fptags[3]=1;
            expected=machine;
        }
        assert(!s.mmx_entered && !memcmp(&machine,&expected,sizeof(machine)));
        unsigned prior_stores=stores;
        unsigned prior_flushes=flushes;
        gen_enter_mmx(&s);helper_enter_mmx(&expected);
        assert(!memcmp(&machine,&expected,sizeof(machine)) && flushes==prior_flushes+1 && stores==prior_stores+1);
        /* Faulting restore never changes architectural state. */
        CPUX86State before=machine;s.flags=HF_TS_MASK;
        gen_FXRSTOR(&s,&decode);assert(exceptions==1 && !memcmp(&before,&machine,sizeof(machine)));
        cases++;
    }
    printf("PASS: %u MMX entry/helper state-transition cases; all TOP/tag masks, repeated entry, pending payload/FIP, EMMS, restores and faults\n",cases);
}

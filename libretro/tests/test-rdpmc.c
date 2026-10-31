/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#define R_EAX 0
#define R_ECX 1
#define R_EDX 2
#define CR4_PCE_MASK (1u<<8)
#define HF_CPL_MASK 3
#define EXCP0D_GPF 13
#define SVM_EXIT_RDPMC 0x6f
#define GETPC() 0x12345678u
#define LOG_UNIMP 1
#define qemu_log_mask(...) (logs++)
typedef struct {uint32_t regs[16],cr[8],hflags,eflags,eip;} CPUX86State;
static jmp_buf escape;
static int fault;static bool intercepted;static unsigned intercepts,logs;
static void raise_exception_ra(CPUX86State *env,int exception,uintptr_t pc) {
    assert(exception==EXCP0D_GPF && pc==GETPC());fault=exception;longjmp(escape,1);
}
static void cpu_svm_check_intercept_param(CPUX86State *env,unsigned event,unsigned parameter,uintptr_t pc) {
    assert(event==SVM_EXIT_RDPMC && parameter==0 && pc==GETPC());intercepts++;
    if(intercepted) {fault=-1;longjmp(escape,1);}
}
/* HELPER */
static int invoke(CPUX86State *env) {
    fault=0;if(setjmp(escape)) return fault;helper_rdpmc(env);return 0;
}
typedef struct {struct {int is_jmp;} base;} DisasContext;
typedef struct {int unused;} X86DecodedInsn;
enum {DISAS_NEXT,DISAS_NORETURN};
static int tcg_env;static unsigned step;
static void gen_update_cc_op(DisasContext *s) {assert(step++==0);}
static void gen_update_eip_cur(DisasContext *s) {assert(step++==1);}
static void translator_io_start(void *base) {assert(step++==2);}
static void gen_helper_rdpmc(int env) {assert(step++==3);}
/* TRANSLATOR */
static uint32_t rng=0x12ab34;
static uint32_t next(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void check(unsigned cpl,bool pce,unsigned index,bool svm) {
    CPUX86State env;
    for(unsigned i=0;i<16;i++) env.regs[i]=next();
    for(unsigned i=0;i<8;i++) env.cr[i]=next();
    env.hflags=(next()&~3u)|cpl;env.eflags=next();env.eip=next();
    env.cr[4]=(env.cr[4]&~CR4_PCE_MASK)|(pce?CR4_PCE_MASK:0);env.regs[R_ECX]=index;
    CPUX86State expected=env;intercepted=svm;intercepts=logs=0;
    bool denied=cpl!=0 && !pce;
    int wanted=denied?13:svm?-1:index>1?13:0;
    if(!wanted) expected.regs[R_EAX]=expected.regs[R_EDX]=0;
    assert(invoke(&env)==wanted);assert(!memcmp(&env,&expected,sizeof(env)));
    assert(intercepts==(unsigned)!denied && !logs);
}
int main(void) {
    const unsigned indices[]={0,1,2,3,0x40000000u,0x7fffffffu,0x80000000u,0x80000001u,0xffffffffu};
    unsigned cases=0;
    for(unsigned cpl=0;cpl<4;cpl++) for(unsigned pce=0;pce<2;pce++) for(unsigned svm=0;svm<2;svm++)
    for(unsigned i=0;i<sizeof(indices)/sizeof(indices[0]);i++) {
        check(cpl,pce,indices[i],svm);cases++;
    }
    for(unsigned i=0;i<100000;i++) {
        uint32_t choice=next();check(choice&3,choice&4,(choice&8)?next():(choice>>4)&1,choice&0x20);cases++;
    }
    DisasContext context={0};X86DecodedInsn decode={0};step=0;gen_RDPMC(&context,&decode);
    assert(step==4 && context.base.is_jmp==DISAS_NEXT);
    printf("RDPMC: %u privilege/index/intercept/register-preservation cases; returning translator metadata and zero logging passed\n",cases);
}

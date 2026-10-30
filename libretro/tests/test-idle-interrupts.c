/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdio.h>
enum { IRQ=1, STOP=2, EXITTB=4 };
typedef struct { int value; } TranslationBlock;
typedef struct { int interrupt_request,exit_request,exception; bool deliverable,idle; } CPUState;
static CPUState cpu;
static TranslationBlock block;
static jmp_buf escape;
static unsigned handlers,proofs,exits;
static bool cpu_test_interrupt(CPUState *p,int mask) { return (p->interrupt_request&mask)!=0; }
static int retro_atomic_load_acquire_int(const int *p) { return *p; }
static bool cpu_handle_interrupt(CPUState *p,TranslationBlock **last_tb) {
    handlers++;
    if(p->interrupt_request&STOP) { p->exception=STOP;return true; }
    if((p->interrupt_request&IRQ) && p->deliverable) {
        p->interrupt_request&=~IRQ;p->idle=false;*last_tb=NULL;
    }
    if(p->interrupt_request&EXITTB) { p->interrupt_request&=~EXITTB;*last_tb=NULL; }
    if(p->exit_request) { p->exception=8;return true; }
    return false;
}
static _Noreturn void cpu_loop_exit(CPUState *p) { assert(p->exception);exits++;longjmp(escape,1); }
static bool xemu_libretro_kernel_idle(CPUState *p) { proofs++;return p->idle; }
/* PRODUCTION */
int main(void) {
    static unsigned cases,mask,stop,delivery,idle;
    for(mask=0;mask<8;mask++) for(stop=0;stop<2;stop++)
    for(delivery=0;delivery<2;delivery++) for(idle=0;idle<2;idle++) {
        cpu=(CPUState){.interrupt_request=mask,.exit_request=stop,.deliverable=delivery,.idle=idle};
        handlers=proofs=exits=0;TranslationBlock *last=&block;
        bool should_exit=(mask&STOP)||stop;
        if(setjmp(escape)==0) {
            bool result=cpu_kernel_idle_wait(&cpu,&last);
            assert(!should_exit && proofs==1 && !exits);
            assert(result==(bool)(idle && !((mask&IRQ)&&delivery)));
            if((mask&EXITTB)||((mask&IRQ)&&delivery)) assert(last==NULL);
        } else {
            assert(should_exit && exits==1 && !proofs && cpu.exception);
        }
        assert(handlers==(unsigned)(mask!=0 || stop));
        cases++;
    }
    printf("idle interrupt ordering: %u masked/deliverable/stop/active-code cases passed\n",cases);
    return 0;
}

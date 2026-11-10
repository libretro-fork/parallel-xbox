/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#ifdef __x86_64__
#include <xmmintrin.h>
#endif
#ifndef X87_COMPARE_ITERATIONS
#define X87_COMPARE_ITERATIONS 2000000
#endif
static unsigned calls;
int xemu_cpu_boost;
G_NORETURN void g_assertion_message_expr(const char *domain,const char *file,int line,const char *func,const char *expr)
{fprintf(stderr,"%s:%d %s: %s\n",file,line,func,expr);abort();}
static FloatRelation fallback_compare(floatx80 a,floatx80 b,float_status *s)
{calls++;return floatx80_compare_quiet(a,b,s);}
/* PRODUCTION */
static uint64_t rng=0xa93b711;
static uint64_t next(void) {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static bool ordinary(floatx80 a) {
    unsigned exponent=a.high&32767;
    return exponent==0?a.low==0:exponent<32767&&!!(a.low&UINT64_C(0x8000000000000000));
}
int main(void) {
    const floatx80 edges[]={make_floatx80_init(0,0),make_floatx80_init(0x8000,0),
        make_floatx80_init(0,1),make_floatx80_init(0x8000,1),
        make_floatx80_init(0,0x8000000000000000ull),make_floatx80_init(0x8000,0xffffffffffffffffull),
        make_floatx80_init(1,0x8000000000000000ull),make_floatx80_init(0x8001,0x8000000000000000ull),
        make_floatx80_init(0x3fff,0x8000000000000000ull),make_floatx80_init(0xbfff,0x8000000000000000ull),
        make_floatx80_init(0x7ffe,UINT64_MAX),make_floatx80_init(0xfffe,UINT64_MAX),
        make_floatx80_init(0x7fff,0x8000000000000000ull),make_floatx80_init(0xffff,0x8000000000000000ull),
        make_floatx80_init(0x7fff,0xc123456789abcdefull),make_floatx80_init(0xffff,0x8000000000000001ull),
        make_floatx80_init(0x7fff,1),make_floatx80_init(0xffff,0),
        make_floatx80_init(0x3fff,1),make_floatx80_init(0xbfff,0x7fffffffffffffffull)};
    const FloatRoundMode rounds[]={float_round_nearest_even,float_round_down,float_round_up,float_round_to_zero};
    const FloatX80RoundPrec precisions[]={floatx80_precision_s,floatx80_precision_d,floatx80_precision_x};
    uint64_t cases=0,fast=0,fallback=0;
#ifdef __x86_64__
    unsigned original_mxcsr=_mm_getcsr();
#endif
    for(unsigned mode=0;mode<12;mode++)for(unsigned i=0;i<X87_COMPARE_ITERATIONS;i++) {
        floatx80 a=make_floatx80(next(),next()),b=make_floatx80(next(),next());
        if(i<ARRAY_SIZE(edges)*ARRAY_SIZE(edges)) {a=edges[i/ARRAY_SIZE(edges)];b=edges[i%ARRAY_SIZE(edges)];}
        else if(i%4==0) {
            a.high=(next()&0x8000)|(1+next()%32766);a.low=next()|UINT64_C(0x8000000000000000);
            b.high=(next()&0x8000)|(1+next()%32766);b.low=next()|UINT64_C(0x8000000000000000);
        } else if(i%4==1) {a.high=i;b.high=(uint16_t)~i;}
        else if(i%4==2) {b=a;if(i&4)b.high^=0x8000;}
        float_status got={0};
        set_float_rounding_mode(rounds[mode%4],&got);
        set_floatx80_rounding_precision(precisions[mode/4],&got);
        set_float_2nan_prop_rule(float_2nan_prop_x87,&got);
        set_float_default_nan_pattern(0xc0,&got);
        set_float_exception_flags(i&0x7fff,&got);
        float_status expected=got;calls=0;
#ifdef __x86_64__
        unsigned mxcsr=0x1f80|((i&3)<<13)|(i&0x3f);
        _mm_setcsr(mxcsr);
        uint16_t cw_before,cw_after;
        __asm__ volatile("fnstcw %0":"=m"(cw_before));
#endif
        FloatRelation actual=floatx80_compare_quiet__hard(a,b,&got);
#ifdef __x86_64__
        __asm__ volatile("fnstcw %0":"=m"(cw_after));
        assert(_mm_getcsr()==mxcsr&&cw_before==cw_after);
#endif
        FloatRelation want=floatx80_compare_quiet(a,b,&expected);
        assert(actual==want&&!memcmp(&got,&expected,sizeof(got)));
        bool normal=ordinary(a)&&ordinary(b);
        assert(calls==(unsigned)!normal);
        if(normal)fast++;else fallback++;
        cases++;
    }
#ifdef __x86_64__
    _mm_setcsr(original_mxcsr);
#endif
    printf("PASS: %llu x87 quiet comparisons against real SoftFloat; %llu integer fast/%llu exact fallbacks; all PC/RC, sticky flags, special classes and host control state\n",
           (unsigned long long)cases,(unsigned long long)fast,(unsigned long long)fallback);
}

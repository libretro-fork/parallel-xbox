/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#ifndef X87_INT_ITERATIONS
#define X87_INT_ITERATIONS 100000
#endif
int xemu_cpu_boost;
G_NORETURN void g_assertion_message_expr(const char *domain,const char *file,int line,const char *func,const char *expr)
{fprintf(stderr,"%s:%d %s: %s\n",file,line,func,expr);abort();}
typedef struct {float_status fp_status;uint16_t fpuc,fpus;unsigned fpstt;struct {floatx80 d;} fpregs[8];} CPUX86State;
#define ST0 (env->fpregs[env->fpstt].d)
/* PRODUCTION */
static uint64_t seed=0x123bce;
static uint64_t rnd(void){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;}
static uint16_t status_bits(int flags)
{
    const unsigned sf[]={float_flag_invalid,float_flag_input_denormal_used,float_flag_divbyzero,float_flag_overflow,float_flag_underflow,float_flag_inexact};
    uint16_t result=0;for(unsigned i=0;i<6;i++)if(flags&sf[i])result|=1u<<i;
    return result;
}
int main(void)
{
    const floatx80 edges[]={make_floatx80_init(0,0),make_floatx80_init(0x8000,0),
        make_floatx80_init(0x3fff,0xc000000000000000ull),make_floatx80_init(0xbfff,0xc000000000000000ull),
        make_floatx80_init(0x400d,0xffff000000000000ull),make_floatx80_init(0x400e,0x8000000000000000ull),
        make_floatx80_init(0x401d,0xffffffff00000000ull),make_floatx80_init(0x401e,0x8000000000000000ull),
        make_floatx80_init(0x403d,0xffffffffffffffffull),make_floatx80_init(0x403e,0x8000000000000000ull),
        make_floatx80_init(0,1),make_floatx80_init(0x8000,1),make_floatx80_init(0,0x8000000000000000ull),
        make_floatx80_init(1,0x8000000000000000ull),make_floatx80_init(0x7fff,0x8000000000000000ull),
        make_floatx80_init(0xffff,0x8000000000000000ull),make_floatx80_init(0x7fff,0xc123456789abcdefull),
        make_floatx80_init(0x7fff,0x8000000000000001ull),make_floatx80_init(0x3fff,1),make_floatx80_init(0x7ffe,UINT64_MAX)};
    const FloatRoundMode rounds[]={float_round_nearest_even,float_round_down,float_round_up,float_round_to_zero};
    const FloatX80RoundPrec pc[]={floatx80_precision_s,floatx80_precision_d,floatx80_precision_x};
    uint64_t cases=0;
    for(unsigned mode=0;mode<12;mode++)for(unsigned i=0;i<X87_INT_ITERATIONS;i++) {
        floatx80 input=i<ARRAY_SIZE(edges)?edges[i]:make_floatx80(rnd(),rnd());
        if(i>=ARRAY_SIZE(edges)&&i%4==0)input.high=(rnd()&0x8000)|(0x3ffe + rnd()%70);
        for(unsigned kind=0;kind<6;kind++) {
            CPUX86State env={0};env.fpuc=0x300|(i&63);env.fpus=(uint16_t)rnd();env.fpstt=i&7;
            env.fpregs[env.fpstt].d=input;
            set_float_rounding_mode(rounds[mode%4],&env.fp_status);
            set_floatx80_rounding_precision(pc[mode/4],&env.fp_status);
            set_float_2nan_prop_rule(float_2nan_prop_x87,&env.fp_status);
            set_float_default_nan_pattern(0xc0,&env.fp_status);
            int old=(i*73+kind*11)&0x7fff;set_float_exception_flags(old,&env.fp_status);
            CPUX86State expected=env;set_float_exception_flags(0,&expected.fp_status);
            bool trunc=kind>=3;unsigned width=kind%3;
            int64_t want=width==2 ?
                (trunc?floatx80_to_int64_round_to_zero(input,&expected.fp_status):floatx80_to_int64(input,&expected.fp_status)) :
                (trunc?floatx80_to_int32_round_to_zero(input,&expected.fp_status):floatx80_to_int32(input,&expected.fp_status));
            int flags=get_float_exception_flags(&expected.fp_status);
            if(width==0 && (want<INT16_MIN || want>INT16_MAX)) {want=INT16_MIN;flags=float_flag_invalid;}
            if(width && (flags&float_flag_invalid))want=width==2?INT64_MIN:INT32_MIN;
            set_float_exception_flags(flags|old,&expected.fp_status);
            expected.fpus|=status_bits(flags);
            if(expected.fpus & (~expected.fpuc & 63))expected.fpus|=0x8080;
            int64_t actual;
            switch(kind) {
            case 0:actual=helper_fist_ST0(&env);break;
            case 1:actual=helper_fistl_ST0(&env);break;
            case 2:actual=helper_fistll_ST0(&env);break;
            case 3:actual=helper_fistt_ST0(&env);break;
            case 4:actual=helper_fisttl_ST0(&env);break;
            default:actual=helper_fisttll_ST0(&env);break;
            }
            assert(actual==want && !memcmp(&env,&expected,sizeof(env)));cases++;
        }
    }
    printf("PASS: %llu x87 integer-helper result/full-state cases against real SoftFloat; all widths, RC/PC, truncation, indefinite values, masks and sticky flags\n",(unsigned long long)cases);
    return 0;
}

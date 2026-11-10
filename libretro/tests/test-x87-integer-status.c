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
#define ST1 (env->fpregs[(env->fpstt+1)&7].d)
typedef uint64_t target_ulong;
typedef union {floatx80 d;struct {uint64_t lower;uint16_t upper;} l;} CPU_LDoubleU;
#define SIGND(fp) ((fp.l.upper)&0x8000)
#define MMU_DATA_STORE 1
#define GETPC() 0x1234
typedef struct {CPUX86State *env;target_ulong start;} X86Access;
static uint8_t memory[32];static unsigned stores;static uint16_t status_before_store;
static void access_prepare(X86Access *ac,CPUX86State *env,target_ulong ptr,unsigned size,unsigned mode,uintptr_t ra)
{assert(ptr==8 && size==10 && mode==MMU_DATA_STORE && ra==GETPC());ac->env=env;ac->start=ptr;stores=0;status_before_store=env->fpus;}
static void access_stb(X86Access *ac,target_ulong ptr,uint32_t value)
{assert(ptr>=ac->start && ptr<ac->start+10 && ac->env->fpus==status_before_store);memory[ptr]=value;stores++;}
/* PRODUCTION */
static uint64_t seed=0x123bce;
static uint64_t rnd(void){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;}
static uint16_t status_bits(int flags)
{
    const unsigned sf[]={float_flag_invalid,float_flag_input_denormal_used,float_flag_divbyzero,float_flag_overflow,float_flag_underflow,float_flag_inexact};
    uint16_t result=0;for(unsigned i=0;i<6;i++)if(flags&sf[i])result|=1u<<i;
    return result;
}
static void oracle_merge(CPUX86State *expected,int flags,int old)
{
    set_float_exception_flags(flags|old,&expected->fp_status);
    expected->fpus|=status_bits(flags);
    if(expected->fpus & (~expected->fpuc & 63))expected->fpus|=0x8080;
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
        for(unsigned kind=0;kind<9;kind++) {
            CPUX86State env={0};env.fpuc=0x300|(i&63);env.fpus=(uint16_t)rnd();env.fpstt=i&7;
            env.fpregs[env.fpstt].d=input;
            set_float_rounding_mode(rounds[mode%4],&env.fp_status);
            set_floatx80_rounding_precision(pc[mode/4],&env.fp_status);
            set_float_2nan_prop_rule(float_2nan_prop_x87,&env.fp_status);
            set_float_default_nan_pattern(0xc0,&env.fp_status);
            int old=(i*73+kind*11)&0x7fff;set_float_exception_flags(old,&env.fp_status);
            CPUX86State expected=env;set_float_exception_flags(0,&expected.fp_status);
            if(kind>=6) {
                uint8_t wanted[32];memset(memory,0xa5,sizeof(memory));memcpy(wanted,memory,sizeof(wanted));
                if(kind==6) {
                    expected.fpregs[env.fpstt].d=floatx80_round_to_int(input,&expected.fp_status);
                } else if(kind==7) {
                    int64_t integer=floatx80_to_int64(input,&expected.fp_status);
                    memset(wanted+8,0,10);
                    if(integer>=INT64_C(1000000000000000000) || integer<=-INT64_C(1000000000000000000)) {
                        set_float_exception_flags(float_flag_invalid,&expected.fp_status);
                        wanted[15]=0xc0;wanted[16]=wanted[17]=0xff;
                    } else {
                        uint64_t magnitude=integer<0?(uint64_t)-integer:(uint64_t)integer,divisor=1;
                        for(unsigned digit=0;digit<18;digit++) {
                            wanted[8+digit/2]|=((magnitude/divisor)%10)<<(4*(digit%2));divisor*=10;
                        }
                        wanted[17]=(input.high&0x8000)?0x80:0;
                    }
                } else {
                    const int exponent[]={-20000,-1,0,1,20000,INT32_MIN,INT32_MAX};
                    int n=exponent[i%ARRAY_SIZE(exponent)];
                    float_status temp={0};set_floatx80_rounding_precision(floatx80_precision_x,&temp);
                    env.fpregs[(env.fpstt+1)&7].d=int32_to_floatx80(n,&temp);
                    expected.fpregs[(env.fpstt+1)&7].d=env.fpregs[(env.fpstt+1)&7].d;
                    set_floatx80_rounding_precision(floatx80_precision_x,&expected.fp_status);
                    expected.fpregs[env.fpstt].d=floatx80_scalbn(input,n,&expected.fp_status);
                    set_floatx80_rounding_precision(pc[mode/4],&expected.fp_status);
                }
                int flags=get_float_exception_flags(&expected.fp_status);oracle_merge(&expected,flags,old);
                if(kind==6)helper_frndint(&env);
                else if(kind==7){helper_fbst_ST0(&env,8);assert(stores==10 && !memcmp(memory,wanted,sizeof(memory)));}
                else helper_fscale(&env);
                assert(env.fpregs[env.fpstt].d.low==expected.fpregs[env.fpstt].d.low && env.fpregs[env.fpstt].d.high==expected.fpregs[env.fpstt].d.high);
                /* Only the ten architectural result bytes matter, not the
                 * unused padding in a returned floatx80 union. */
                memcpy(&expected.fpregs[env.fpstt].d,&env.fpregs[env.fpstt].d,sizeof(floatx80));
                assert(!memcmp(&env,&expected,sizeof(env)));cases++;continue;
            }
            bool trunc=kind>=3;unsigned width=kind%3;
            int64_t want=width==2 ?
                (trunc?floatx80_to_int64_round_to_zero(input,&expected.fp_status):floatx80_to_int64(input,&expected.fp_status)) :
                (trunc?floatx80_to_int32_round_to_zero(input,&expected.fp_status):floatx80_to_int32(input,&expected.fp_status));
            int flags=get_float_exception_flags(&expected.fp_status);
            if(width==0 && (want<INT16_MIN || want>INT16_MAX)) {want=INT16_MIN;flags=float_flag_invalid;}
            if(width && (flags&float_flag_invalid))want=width==2?INT64_MIN:INT32_MIN;
            oracle_merge(&expected,flags,old);
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
    printf("PASS: %llu x87 integer/round/BCD/scale result and architectural-state cases against real SoftFloat; all widths, RC/PC, masks, sticky flags and guarded stores\n",(unsigned long long)cases);
    return 0;
}

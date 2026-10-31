/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#include <fenv.h>
#include <math.h>
#define FPUC_EM 0x3f
#define FPUS_IE 1
#define FPUS_DE 2
#define FPUS_ZE 4
#define FPUS_OE 8
#define FPUS_UE 16
#define FPUS_PE 32
#define FPUS_SE 0x80
#define FPUS_B 0x8000
typedef struct {float_status fp_status;floatx80 ft0;struct {floatx80 d;} fpregs[8];uint32_t fpstt;uint8_t fptags[8];uint16_t fpus,fpuc;} CPUX86State;
#define FT0 (env->ft0)
int xemu_cpu_boost;
void g_assertion_message_expr(const char *domain,const char *file,int line,const char *func,const char *expr) {fprintf(stderr,"%s:%d %s: %s\n",file,line,func,expr);abort();}
/* SOFT HELPERS */

/* Evaluate the production TCG generators immediately. Arithmetic/conversions
 * use native SSE rounding; the independent oracle uses actual SoftFloat. */
typedef double *TCGv_f64;
typedef float *TCGv_f32;
typedef uint32_t *TCGv_i32;
typedef uint64_t *TCGv_i64;
typedef void *TCGv_ptr;
typedef void *TCGv_fp;
typedef struct {uint32_t flags;int fpstt_delta;void *ft0,*fpregs[8];bool flcr_set;} DisasContext;
#define HF_FPU_PC_MASK 0x300
static CPUX86State machine;
static void *tcg_env=&machine;
static uint32_t *fpstt=&machine.fpstt;
static bool g_use_hard_fpu=true;
static unsigned temps,rounds,loads,flushes;
static union {double d;float f;uint64_t q;uint32_t l;} arena[2048];
static void *temp(void) {assert(temps<2048);return &arena[temps++];}
static TCGv_f64 tcg_temp_new_f64(void) {return temp();}
static TCGv_f32 tcg_temp_new_f32(void) {return temp();}
static TCGv_i64 tcg_temp_new_i64(void) {return temp();}
static TCGv_i32 tcg_constant_i32(uint32_t v) {TCGv_i32 p=temp();*p=v;return p;}
static TCGv_ptr gen_ft0_ptr(void) {return &machine.ft0;}
static TCGv_ptr gen_stn_ptr(int n) {return &machine.fpregs[(machine.fpstt+n)&7].d;}
static void gen_flcr(DisasContext *s) {}
static void gen_flush_fip(DisasContext *s) {flushes++;}
static void gen_set_fptag(int n,int v) {machine.fptags[(machine.fpstt+n)&7]=v;}
static void tcg_gen_ld80f_f64(TCGv_f64 d,void *p) {*d=(double)((floatx80 *)p)->fval;loads++;}
static void tcg_gen_st80f_f64(TCGv_f64 s,void *p) {((floatx80 *)p)->fval=*s;}
static void tcg_gen_cvt64f_f32(TCGv_f32 d,TCGv_f64 s) {volatile float f=*s;*d=f;rounds++;}
static void tcg_gen_cvt32f_f64(TCGv_f64 d,TCGv_f32 s) {*d=*s;}
static void tcg_gen_mov64i_f64(TCGv_f64 d,TCGv_i64 s) {memcpy(d,s,8);}
static void tcg_gen_mov64f_i64(TCGv_i64 d,TCGv_f64 s) {memcpy(d,s,8);}
static void tcg_gen_mov32i_f32(TCGv_f32 d,TCGv_i32 s) {memcpy(d,s,4);}
static void tcg_gen_mov32f_i32(TCGv_i32 d,TCGv_f32 s) {memcpy(d,s,4);}
static void gen_mov32i_f64(TCGv_f64 d,TCGv_i32 s) {float f;memcpy(&f,s,4);*d=f;}
static void gen_mov64i_f64(TCGv_f64 d,TCGv_i64 s) {tcg_gen_mov64i_f64(d,s);}
static void gen_mov64f_i32(TCGv_i32 d,TCGv_f64 s) {volatile float f=*s;memcpy(d,(const void *)&f,4);}
static void gen_mov64f_i64(TCGv_i64 d,TCGv_f64 s) {tcg_gen_mov64f_i64(d,s);}
static void gen_movi_f64(DisasContext *s,TCGv_f64 d,double v) {*d=v;}
#define FP_BINARY(name,op) static void tcg_gen_##name##_f64(TCGv_f64 d,TCGv_f64 a,TCGv_f64 b) {*d=*a op *b;}
FP_BINARY(add,+) FP_BINARY(sub,-) FP_BINARY(mul,*) FP_BINARY(div,/)
static void tcg_gen_mov_f64(TCGv_f64 d,TCGv_f64 s) {*d=*s;}
static void tcg_gen_chs_f64(TCGv_f64 d,TCGv_f64 s) {*d=-*s;}
static void tcg_gen_abs_f64(TCGv_f64 d,TCGv_f64 s) {*d=fabs(*s);}
static void tcg_gen_sqrt_f64(TCGv_f64 d,TCGv_f64 s) {*d=sqrt(*s);}
static void tcg_gen_sin_f64(TCGv_f64 d,TCGv_f64 s) {*d=sin(*s);}
static void tcg_gen_cos_f64(TCGv_f64 d,TCGv_f64 s) {*d=cos(*s);}
static void tcg_gen_cvt32i_f64(TCGv_f64 d,TCGv_i32 s) {*d=(int32_t)*s;}
static void tcg_gen_cvt64i_f64(TCGv_f64 d,TCGv_i64 s) {*d=(int64_t)*s;}
static void tcg_gen_cvt64f_i32(TCGv_i32 d,TCGv_f64 s) {*d=lrint(*s);}
static void tcg_gen_cvt64f_i64(TCGv_i64 d,TCGv_f64 s) {*d=llrint(*s);}
static void tcg_gen_com_f64(TCGv_i64 d,TCGv_f64 a,TCGv_f64 b) {*d=isnan(*a)||isnan(*b)?0x45:*a<*b?1:*a==*b?0x40:0;}
static void tcg_gen_andi_i64(TCGv_i64 d,TCGv_i64 s,uint64_t v) {*d=*s&v;}
static void tcg_gen_shli_i64(TCGv_i64 d,TCGv_i64 s,unsigned v) {*d=*s<<v;}
static void tcg_gen_or_i64(TCGv_i64 d,TCGv_i64 a,TCGv_i64 b) {*d=*a|*b;}
static void tcg_gen_ld16u_i64(TCGv_i64 d,void *p,size_t off) {uint16_t v;memcpy(&v,(char *)p+off,2);*d=v;}
static void tcg_gen_st16_i64(TCGv_i64 s,void *p,size_t off) {uint16_t v=*s;memcpy((char *)p+off,&v,2);}
static void tcg_gen_subi_i32(TCGv_i32 d,TCGv_i32 s,unsigned v) {*d=*s-v;}
static void tcg_gen_addi_i32(TCGv_i32 d,TCGv_i32 s,unsigned v) {*d=*s+v;}
static void tcg_gen_andi_i32(TCGv_i32 d,TCGv_i32 s,unsigned v) {*d=*s&v;}
#define GEN_HELPER_FALLBACK_v_v(...) assert(g_use_hard_fpu)
#define GEN_HELPER_FALLBACK_v_i(...) assert(g_use_hard_fpu)
#define GEN_HELPER_FALLBACK_v_T(...) assert(g_use_hard_fpu)
#define GEN_HELPER_FALLBACK_T_v(...) assert(g_use_hard_fpu)
#define fp_wrapper(f) glue(f,_f64)
#define PREC 64
#include "target/i386/ops_fpu.h"
#undef PREC
/* Soft dispatch is checked through the real load helpers above. These
 * arithmetic helper emissions must not be selected by the hard generator. */
#define SOFT_ST0(n) static void gen_helper_##n(void *p) {abort();}
SOFT_ST0(fadd_ST0_FT0) SOFT_ST0(fmul_ST0_FT0) SOFT_ST0(fcom_ST0_FT0)
SOFT_ST0(fsub_ST0_FT0) SOFT_ST0(fsubr_ST0_FT0) SOFT_ST0(fdiv_ST0_FT0) SOFT_ST0(fdivr_ST0_FT0)
#define SOFT_STN(n) static void gen_helper_##n(void *p,TCGv_i32 i) {abort();}
SOFT_STN(fadd_STN_ST0) SOFT_STN(fmul_STN_ST0) SOFT_STN(fsub_STN_ST0) SOFT_STN(fsubr_STN_ST0) SOFT_STN(fdiv_STN_ST0) SOFT_STN(fdivr_STN_ST0)
/* TRANSLATOR */
static uint64_t rng=0x15ab87;
static uint64_t next(void) {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static uint64_t bits(double x) {uint64_t v;memcpy(&v,&x,8);return v;}
static DisasContext reset(unsigned pc,unsigned rc) {
    memset(&machine,0,sizeof(machine));temps=rounds=loads=flushes=0;machine.fpuc=0x3f;
    set_floatx80_rounding_precision(pc==0?floatx80_precision_s:pc==2?floatx80_precision_d:floatx80_precision_x,&machine.fp_status);
    set_float_rounding_mode(rc,&machine.fp_status);set_float_2nan_prop_rule(float_2nan_prop_x87,&machine.fp_status);set_float_default_nan_pattern(0xc0,&machine.fp_status);
    static const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};assert(!fesetround(modes[rc]));
    return (DisasContext){.flags=pc<<8};
}
static void hard_load(DisasContext *s,double x) {uint64_t v=bits(x);gen_fldl_ST0(s,&v);}
static double hard_read(DisasContext *s) {return *get_st0_f64(s);}
static void same80(floatx80 a,floatx80 b) {assert(a.low==b.low && a.high==b.high);}
static double reference(double a,double b,unsigned op,unsigned pc,unsigned rc) {
    float_status st=machine.fp_status;
    set_floatx80_rounding_precision(floatx80_precision_x,&st);
    floatx80 x=float64_to_floatx80(bits(a),&st),y=float64_to_floatx80(bits(b),&st),z;
    set_floatx80_rounding_precision(pc==0?floatx80_precision_s:floatx80_precision_d,&st);
    switch(op) {case 0:z=floatx80_add(x,y,&st);break;case 1:z=floatx80_mul(x,y,&st);break;
    case 4:z=floatx80_sub(x,y,&st);break;case 5:z=floatx80_sub(y,x,&st);break;case 6:z=floatx80_div(x,y,&st);break;default:z=floatx80_div(y,x,&st);break;}
    uint64_t v=floatx80_to_float64(z,&st);double result;memcpy(&result,&v,8);return result;
}
int main(void) {
    unsigned soft_cases=0,hard_cases=0;
    const unsigned pcs[]={0,2,3};
    for(unsigned mode=0;mode<3;mode++) for(unsigned rc=0;rc<4;rc++) for(unsigned i=0;i<3000;i++) {
        DisasContext s=reset(pcs[mode],rc);uint64_t raw=i?next():bits(314159265.35897934);
        /* Independent native widening for finite binary64, including subnormals. */
        if((raw&0x7ff0000000000000ull)==0x7ff0000000000000ull) raw^=1ull<<52;
        double input;memcpy(&input,&raw,8);floatx80 expected={.fval=(long double)input};
        FloatX80RoundPrec precision=get_floatx80_rounding_precision(&machine.fp_status);
        set_float_exception_flags(float_flag_divbyzero,&machine.fp_status);
        helper_fldl_FT0(&machine,raw);same80(machine.ft0,expected);
        assert(get_floatx80_rounding_precision(&machine.fp_status)==precision);
        assert(get_float_exception_flags(&machine.fp_status)&float_flag_divbyzero);
        machine.fpstt=i&7;unsigned top=(machine.fpstt-1)&7;memset(machine.fptags,1,8);
        helper_fldl_ST0(&machine,raw);same80(machine.fpregs[top].d,expected);assert(machine.fpstt==top && !machine.fptags[top]);
        assert(get_floatx80_rounding_precision(&machine.fp_status)==precision);soft_cases++;
        /* Hard loads/moves must retain all input bits regardless of PC. */
        s=reset(pcs[mode],rc);xemu_cpu_boost=i&1;hard_load(&s,input);assert(bits(hard_read(&s))==raw && !rounds);
        gen_fchs_ST0(&s);gen_fabs_ST0(&s);assert(bits(hard_read(&s))==(raw&0x7fffffffffffffffull));
        gen_flush_fp(&s);assert(!s.ft0 && !s.fpregs[0] && !s.fpregs[7] && flushes==1);
        assert(bits((double)machine.fpregs[machine.fpstt].d.fval)==(raw&0x7fffffffffffffffull));hard_cases++;
    }
    const unsigned ops[]={0,1,4,5,6,7};
    for(unsigned pc=0;pc<3;pc+=2) for(unsigned rc=0;rc<4;rc++) for(unsigned i=0;i<1000;i++) for(unsigned o=0;o<6;o++) {
        unsigned op=ops[o];DisasContext s=reset(pc,rc);xemu_cpu_boost=i&1;
        double a=(int32_t)next()/1024.0,b=(int32_t)(next()|1)/2048.0;
        hard_load(&s,a);uint64_t raw=bits(b);unsigned before=loads;gen_fldl_FT0(&s,&raw);
        assert(loads==before+(xemu_cpu_boost?0:1));
        double expected=reference(a,b,op,pc,rc);gen_helper_fp_arith_ST0_FT0(&s,op);
        assert(bits(hard_read(&s))==bits(expected) && rounds==(pc==0));hard_cases++;
        s=reset(pc,rc);hard_load(&s,a);hard_load(&s,b);
        expected=reference(a,b,(op==4?5:op==5?4:op==6?7:op==7?6:op),pc,rc);
        gen_helper_fp_arith_STN_ST0(&s,op,1);assert(bits(*get_stn_f64(&s,1))==bits(expected) && rounds==(pc==0));hard_cases++;
    }
    for(unsigned rc=0;rc<4;rc++) {
        DisasContext s=reset(0,rc);hard_load(&s,1.0+0x1p-30);uint64_t one=bits(1);gen_fldl_FT0(&s,&one);
        gen_helper_fp_arith_ST0_FT0(&s,2);assert((machine.fpus&0x4500)==0 && !rounds && hard_read(&s)>1);
        gen_helper_fp_arith_ST0_FT0(&s,3);assert(!rounds && hard_read(&s)>1);
        s=reset(0,rc);hard_load(&s,314159265.35897934);assert(hard_read(&s)==314159265.35897934);
        uint32_t integer;gen_fistl_ST0(&s,&integer);assert(integer==(rc==2?314159266u:314159265u));
        uint64_t stored;gen_fstl_ST0(&s,&stored);assert(stored==bits(314159265.35897934));
        uint32_t narrow;gen_fsts_ST0(&s,&narrow);float_status st=machine.fp_status;
        assert(narrow==float64_to_float32(bits(314159265.35897934),&st));
        hard_load(&s,1.0+0x1p-30);gen_fxchg_ST0_STN(&s,1);assert(hard_read(&s)==314159265.35897934);
        gen_fmov_FT0_STN(&s,1);assert(*get_ft0_f64(&s)==1.0+0x1p-30);
        gen_fmov_ST0_STN(&s,1);gen_fmov_STN_ST0(&s,1);assert(hard_read(&s)==1.0+0x1p-30 && !rounds);gen_fpop(&s);
        s=reset(0,rc);uint32_t input=16777217;gen_fildl_ST0(&s,&input);assert(hard_read(&s)==16777217 && !rounds);
        uint64_t wide=(1ull<<40)+1;gen_fildll_ST0(&s,&wide);assert(hard_read(&s)==(double)wide && !rounds);
        gen_fistll_ST0(&s,&stored);assert(stored==wide);
        s=reset(0,rc);hard_load(&s,16777216);one=bits(1);gen_fldl_FT0(&s,&one);gen_helper_fp_arith_ST0_FT0(&s,0);
        assert(hard_read(&s)==(rc==2?16777218:16777216) && rounds==1);
    }
    for(unsigned pc=0;pc<3;pc+=2) for(unsigned rc=0;rc<4;rc++) for(unsigned i=0;i<500;i++) {
        DisasContext s=reset(pc,rc);double input=(next()%1000000+1)/128.0;
        hard_load(&s,input);float_status st=machine.fp_status;
        set_floatx80_rounding_precision(floatx80_precision_x,&st);
        floatx80 value=float64_to_floatx80(bits(input),&st);
        set_floatx80_rounding_precision(pc==0?floatx80_precision_s:floatx80_precision_d,&st);
        value=floatx80_sqrt(value,&st);uint64_t wanted=floatx80_to_float64(value,&st);
        gen_fsqrt(&s);assert(bits(hard_read(&s))==wanted && rounds==(pc==0));hard_cases++;
    }
    const uint64_t special[]={0x7ff0000000000000ull,0xfff0000000000000ull,0x7ff123456789abcdull,0x7ff923456789abcdull,0x8000000000000000ull};
    for(unsigned p=0;p<3;p++) for(unsigned rc=0;rc<4;rc++) for(unsigned i=0;i<G_N_ELEMENTS(special);i++) {
        reset(pcs[p],rc);float_status st=machine.fp_status;FloatX80RoundPrec precision=get_floatx80_rounding_precision(&st);
        set_floatx80_rounding_precision(floatx80_precision_x,&st);floatx80 wanted=float64_to_floatx80(special[i],&st);
        helper_fldl_FT0(&machine,special[i]);same80(machine.ft0,wanted);
        assert(get_floatx80_rounding_precision(&machine.fp_status)==precision);
        assert(get_float_exception_flags(&machine.fp_status)==get_float_exception_flags(&st));soft_cases++;
    }
    fesetround(FE_TONEAREST);
    printf("x87 precision: %u real SoftFloat load and %u production hard-cache load/move/arithmetic cases passed; all PC/RC modes, boost on/off and comparisons covered\n",soft_cases,hard_cases);
}

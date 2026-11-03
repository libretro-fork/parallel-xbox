/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#ifdef __x86_64__
#include <xmmintrin.h>
#endif
typedef struct {float_status sse_status; uint64_t cc_src; unsigned cc_op;} CPUX86State;
typedef union {uint32_t l[4];uint64_t q[2];} Reg;
#define ZMM_S(i) l[i]
#define ZMM_D(i) q[i]
#define CC_SRC env->cc_src
#define CC_OP env->cc_op
#define CC_C 1
#define CC_P 4
#define CC_Z 64
#define CC_OP_EFLAGS 1
static unsigned calls;
int xemu_cpu_boost;
void g_assertion_message_expr(const char *domain,const char *file,int line,const char *func,const char *expr)
{fprintf(stderr,"assertion %s:%d %s: %s\n",file,line,func,expr);abort();}
static FloatRelation spy32(float32 a,float32 b,float_status *s) {calls++;return float32_compare(a,b,s);}
static FloatRelation spy32q(float32 a,float32 b,float_status *s) {calls++;return float32_compare_quiet(a,b,s);}
static FloatRelation spy64(float64 a,float64 b,float_status *s) {calls++;return float64_compare(a,b,s);}
static FloatRelation spy64q(float64 a,float64 b,float_status *s) {calls++;return float64_compare_quiet(a,b,s);}
#define float32_compare spy32
#define float32_compare_quiet spy32q
#define float64_compare spy64
#define float64_compare_quiet spy64q
/* PRODUCTION */
#undef float32_compare
#undef float32_compare_quiet
#undef float64_compare
#undef float64_compare_quiet
static uint64_t rng=0x12abcd7654321;
static uint64_t random64(void) {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static const uint64_t edge64[]={0,0x8000000000000000ULL,1,0x8000000000000001ULL,0xfffffffffffffULL,0x800fffffffffffffULL,0x10000000000000ULL,0x8010000000000000ULL,0x3ff0000000000000ULL,0xbff0000000000000ULL,0x7fefffffffffffffULL,0xffefffffffffffffULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff0000000000001ULL,0x7ff8000000000000ULL,0xfff0000000000001ULL,0xfff8000000000000ULL};
static const uint32_t edge32[]={0,0x80000000,1,0x80000001,0x7fffff,0x807fffff,0x800000,0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7f800001,0x7fc00000,0xff800001,0xffc00000};
int main(void)
{
 unsigned cases=0,fast=0,fallback=0;
 for(unsigned mode=0;mode<16;mode++) for(unsigned bits=32;bits<=64;bits+=32)
 for(unsigned quiet=0;quiet<2;quiet++) for(unsigned i=0;i<20000;i++) {
  CPUX86State got={0},ref;
  set_float_2nan_prop_rule(float_2nan_prop_x87,&got.sse_status);
  set_float_default_nan_pattern(0xc0,&got.sse_status);
  set_float_ftz_detection(float_ftz_after_rounding,&got.sse_status);
  got.sse_status.float_rounding_mode=mode&3;
  got.sse_status.flush_to_zero=!!(mode&4);got.sse_status.flush_inputs_to_zero=!!(mode&8);
  got.sse_status.float_exception_flags=i&0x7f;got.cc_src=0x12345678;got.cc_op=99;ref=got;
  Reg a={{0}},b={{0}};
  a.q[0]=random64();a.q[1]=random64();b.q[0]=random64();b.q[1]=random64();
  if(bits==32) {a.l[0]=i<324?edge32[i/18]:random64();b.l[0]=i<324?edge32[i%18]:random64();}
  else {a.q[0]=i<324?edge64[i/18]:random64();b.q[0]=i<324?edge64[i%18]:random64();}
  /* Include aliases and preserve every source register byte. */
  Reg *src=(i&31)?&b:&a; Reg before_a=a,before_b=b;
  bool ordinary=bits==32?(float32_is_zero_or_normal(a.l[0])&&float32_is_zero_or_normal(src->l[0])):(float64_is_zero_or_normal(a.q[0])&&float64_is_zero_or_normal(src->q[0]));
#ifdef __x86_64__
  static const unsigned modes[]={0x1f80,0x3f80,0x5fc0,0xffc0};
  unsigned mxcsr=modes[(i>>5)&3];_mm_setcsr(mxcsr);
#endif
  calls=0;xemu_cpu_boost=i&1;
  if(bits==32) {if(quiet)helper_ucomiss(&got,&a,src);else helper_comiss(&got,&a,src);}
  else {if(quiet)helper_ucomisd(&got,&a,src);else helper_comisd(&got,&a,src);}
  assert(calls==(unsigned)!ordinary);
#ifdef __x86_64__
  if(ordinary)assert(_mm_getcsr()==mxcsr);
#endif
  FloatRelation relation=bits==32?(quiet?float32_compare_quiet(a.l[0],src->l[0],&ref.sse_status):float32_compare(a.l[0],src->l[0],&ref.sse_status)):(quiet?float64_compare_quiet(a.q[0],src->q[0],&ref.sse_status):float64_compare(a.q[0],src->q[0],&ref.sse_status));
  static const unsigned flags[]={CC_C,CC_Z,0,CC_Z|CC_P|CC_C};
  ref.cc_src=flags[relation+1];ref.cc_op=CC_OP_EFLAGS;
  assert(!memcmp(&got,&ref,sizeof(got)) && !memcmp(&a,&before_a,sizeof(a)) && !memcmp(&b,&before_b,sizeof(b)));
  cases++;if(ordinary)fast++;else fallback++;
 }
#ifdef __x86_64__
 _mm_setcsr(0x1f80);
#endif
 printf("SSE ordered compare: %u exact flags/status/source/alias cases; %u ordinary, %u exceptional\n",cases,fast,fallback);
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#include <xmmintrin.h>
/* The real helpers use only these fields and register lane views. */
typedef struct { float_status sse_status; uint32_t xemu_host_mxcsr; } CPUX86State;
typedef union { uint32_t l[16]; uint64_t q[8]; } TestReg;
#define Reg TestReg
#define ZMM_S(i) l[i]
#define ZMM_D(i) q[i]
#define ZMM_L(i) l[i]
#define ZMM_Q(i) q[i]
#ifndef SHIFT
#define SHIFT 1
#endif
#if SHIFT == 1
#define SUFFIX _xmm
#else
#define SUFFIX _ymm
#endif
int xemu_cpu_boost;
static unsigned soft_calls;
/* The full SoftFloat source's only GLib dependency, used for failed assertions. */
void g_assertion_message_expr(const char *domain,const char *file,int line,const char *func,const char *expr) { fprintf(stderr,"assertion %s:%d %s: %s\n",file,line,func,expr); abort(); }
#define SPY(bits,name) \
static float##bits spy_##name##bits(float##bits a,float##bits b,float_status *s) {soft_calls++;return float##bits##_##name(a,b,s);}
SPY(32,add) SPY(32,sub) SPY(32,mul)
SPY(64,add) SPY(64,sub) SPY(64,mul)
#define float32_add spy_add32
#define float32_sub spy_sub32
#define float32_mul spy_mul32
#define float64_add spy_add64
#define float64_sub spy_sub64
#define float64_mul spy_mul64
/* PRODUCTION */
#undef float32_add
#undef float32_sub
#undef float32_mul
#undef float64_add
#undef float64_sub
#undef float64_mul
static uint64_t rng=0x123abcd76543;
static uint64_t random64(void) {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static const uint32_t edge32[]={0,0x80000000,1,0x80000001,0x800000,0x7fffff,0x21800000,0x217fffff,0x5dffffff,0x5e000000,0xa1800000,0xddffffff,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7f800001,0x7fc00000,0x3f800000,0xbf800000};
static const uint64_t edge64[]={0,0x8000000000000000ULL,1,0x8000000000000001ULL,0x10000000000000ULL,0xfffffffffffffULL,0x20b0000000000000ULL,0x20afffffffffffffULL,0x5f3fffffffffffffULL,0x5f40000000000000ULL,0xa0b0000000000000ULL,0xdf3fffffffffffffULL,0x7fefffffffffffffULL,0xffefffffffffffffULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff0000000000001ULL,0x7ff8000000000000ULL,0x3ff0000000000000ULL,0xbff0000000000000ULL};
typedef void (*Fn)(CPUX86State *,TestReg *,TestReg *,TestReg *);
static Fn funcs[]={glue(helper_addps,SUFFIX),glue(helper_subps,SUFFIX),glue(helper_mulps,SUFFIX),glue(helper_addpd,SUFFIX),glue(helper_subpd,SUFFIX),glue(helper_mulpd,SUFFIX),
#if SHIFT == 1
helper_addss,helper_subss,helper_mulss,helper_addsd,helper_subsd,helper_mulsd,
#endif
};
static bool bounded(uint64_t value,unsigned bits) {
 if(bits==32) {unsigned e=((uint32_t)value>>23)&255;return (e>=67&&e<=187)||float32_is_zero(value);}
 unsigned e=(value>>52)&2047;return (e>=523&&e<=1523)||float64_is_zero(value);
}
int main(void) {
 unsigned cases=0,native=0;
 for(unsigned mode=0;mode<24;mode++)for(unsigned boost=0;boost<2;boost++)
 for(unsigned which=0;which<ARRAY_SIZE(funcs);which++)for(unsigned alias=0;alias<4;alias++)for(unsigned pattern=0;pattern<512;pattern++) {
  unsigned kind=which/3,op=which%3,width=(kind&1)?64:32,n=kind<2?((width==32?2:1)<<SHIFT):1;
  TestReg got[3],ref[3];
  for(unsigned j=0;j<3;j++)for(unsigned k=0;k<8;k++)got[j].q[k]=random64();
  for(unsigned j=0;j<3;j++)for(unsigned k=0;k<(width==32?16u:8u);k++) {
   unsigned index=(j==2?pattern%20:pattern/20)+k;
   if(width==32)got[j].l[k]=pattern<400?edge32[index%20]:((pattern&1)?random64():((random64()&0x807fffff)|((67+(random64()%121))<<23)));
   else got[j].q[k]=pattern<400?edge64[index%20]:((pattern&1)?random64():((random64()&0x800fffffffffffffULL)|((uint64_t)(523+(random64()%1001))<<52)));
  }
  memcpy(ref,got,sizeof(got));
  CPUX86State x={0},y;set_float_2nan_prop_rule(float_2nan_prop_x87,&x.sse_status);set_float_default_nan_pattern(0xc0,&x.sse_status);set_float_ftz_detection(float_ftz_after_rounding,&x.sse_status);
  x.sse_status.float_rounding_mode=1+mode%3;x.sse_status.flush_to_zero=(mode/3)&1;x.sse_status.flush_inputs_to_zero=(mode/6)&1;x.sse_status.tininess_before_rounding=mode/12;x.sse_status.float_exception_flags=pattern&0x7fff;x.xemu_host_mxcsr=(pattern&1)?0x1f80:0x3f80;y=x;xemu_cpu_boost=boost;
  static const unsigned aliases[][3]={{0,1,2},{1,1,2},{2,1,2},{1,1,1}};
  unsigned d=aliases[alias][0],v=aliases[alias][1],s=aliases[alias][2],expected=0;
  for(unsigned i=0;i<n;i++)expected+=!(bounded(width==32?got[v].l[i]:got[v].q[i],width)&&bounded(width==32?got[s].l[i]:got[s].q[i],width));
#ifdef __x86_64__
  static const unsigned host_modes[]={0x1f80,0x3f80,0x9fc0,0,0xffff};unsigned csr=host_modes[pattern%5];_mm_setcsr(csr);
#endif
  soft_calls=0;funcs[which](&x,&got[d],&got[v],&got[s]);
  assert(soft_calls==expected);native+=n-expected;
#ifdef __x86_64__
  assert(_mm_getcsr()==csr);
#endif
  for(unsigned i=0;i<n;i++) {
   if(width==32) {float32 a=ref[v].l[i],b=ref[s].l[i];ref[d].l[i]=op==0?float32_add(a,b,&y.sse_status):op==1?float32_sub(a,b,&y.sse_status):float32_mul(a,b,&y.sse_status);}
   else {float64 a=ref[v].q[i],b=ref[s].q[i];ref[d].q[i]=op==0?float64_add(a,b,&y.sse_status):op==1?float64_sub(a,b,&y.sse_status):float64_mul(a,b,&y.sse_status);}
  }
  if(kind>=2) {if(width==32)for(unsigned i=1;i<4;i++)ref[d].l[i]=ref[v].l[i];else ref[d].q[1]=ref[v].q[1];}
  assert(!memcmp(got,ref,sizeof(got))&&!memcmp(&x,&y,sizeof(x)));cases++;
 }
#ifdef __x86_64__
 _mm_setcsr(0x1f80);
#endif
 printf("Directed SSE SHIFT=%u: %u exact result/status/alias/host-state cases; %u native lanes\n",SHIFT,cases,native);
}

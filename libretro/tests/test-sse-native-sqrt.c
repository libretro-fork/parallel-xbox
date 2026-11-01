/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#ifdef __x86_64__
#include <xmmintrin.h>
#endif
typedef struct {float_status sse_status;} CPUX86State;
typedef union {uint32_t l[16];uint64_t q[8];} Reg;
#define ZMM_S(i) l[i]
#define ZMM_D(i) q[i]
#define ZMM_L(i) l[i]
#define ZMM_Q(i) q[i]
#if SHIFT == 1
#define SUFFIX _xmm
#else
#define SUFFIX _ymm
#endif
int xemu_cpu_boost;
static unsigned calls;
void g_assertion_message_expr(const char *domain,const char *file,int line,const char *func,const char *expr)
{fprintf(stderr,"assertion %s:%d %s: %s\n",file,line,func,expr);abort();}
static float32 spy32(float32 a,float_status *s) {calls++;return float32_sqrt(a,s);}
static float64 spy64(float64 a,float_status *s) {calls++;return float64_sqrt(a,s);}
#define float32_sqrt spy32
#define float64_sqrt spy64
/* PRODUCTION */
#undef float32_sqrt
#undef float64_sqrt
static uint64_t rng=0xa1b2c3d4e5;
static uint64_t random64(void) {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static bool eligible(uint64_t bits,unsigned width,unsigned mode) {
#if defined(XBOX) && defined(__x86_64__)
 if(mode>3)return false;
 if(width==32)return float32_is_zero(bits)||(float32_is_normal(bits)&&!float32_is_neg(bits));
 return float64_is_zero(bits)||(float64_is_normal(bits)&&!float64_is_neg(bits));
#else
 return false;
#endif
}
int main(void) {
 static const uint64_t special[]={0,0x8000000000000000ULL,1,0x8000000000000001ULL,0x10000000000000ULL,0x3ff0000000000000ULL,0x4000000000000000ULL,0x7fefffffffffffffULL,0x7ff0000000000000ULL,0x7ff0000000000001ULL,0x7ff8000000000000ULL,0xfff8000000000000ULL,0x80000000,0x800000,0x3f800000,0x40000000,0x7f7fffff,0x7f800000,0x7f800001,0x7fc00000,0xbf800000};
 unsigned cases=0,fast=0;
 for(unsigned mode=0;mode<32;mode++) for(unsigned kind=0;kind<(SHIFT==1?4u:2u);kind++)
 for(unsigned alias=0;alias<4;alias++) for(unsigned pattern=0;pattern<512;pattern++) {
  Reg got[3],ref[3];for(unsigned j=0;j<3;j++)for(unsigned k=0;k<8;k++)got[j].q[k]=pattern<ARRAY_SIZE(special)?special[(pattern+j+k)%ARRAY_SIZE(special)]:random64();
  memcpy(ref,got,sizeof(got));
  CPUX86State x={0},y;
  set_float_2nan_prop_rule(float_2nan_prop_x87,&x.sse_status);set_float_default_nan_pattern(0xc0,&x.sse_status);
  set_float_ftz_detection(float_ftz_after_rounding,&x.sse_status);
  x.sse_status.float_rounding_mode=mode&7;x.sse_status.flush_to_zero=!!(mode&8);x.sse_status.flush_inputs_to_zero=!!(mode&16);
  x.sse_status.float_exception_flags=pattern&0x7fff;y=x;
  static const unsigned aliases[][3]={{0,1,2},{1,1,2},{2,1,2},{1,1,1}};
  unsigned d=aliases[alias][0],v=aliases[alias][1],s=aliases[alias][2];
  unsigned width=(kind&1)?64:32,n=kind<2?((kind==0?2:1)<<SHIFT):1,expected=0;
  for(unsigned k=0;k<n;k++)expected+=!eligible(width==32?got[s].l[k]:got[s].q[k],width,mode&7);
#ifdef __x86_64__
  static const unsigned csr_modes[]={0x1f80,0x3f80,0x9fc0,0,0x6000,0xffff};
  unsigned csr=csr_modes[pattern%ARRAY_SIZE(csr_modes)];
  _mm_setcsr(csr);
#endif
  calls=0;
  if(kind==0)glue(helper_sqrtps,SUFFIX)(&x,&got[d],&got[s]);
  if(kind==1)glue(helper_sqrtpd,SUFFIX)(&x,&got[d],&got[s]);
#if SHIFT == 1
  if(kind==2)helper_sqrtss(&x,&got[d],&got[v],&got[s]);
  if(kind==3)helper_sqrtsd(&x,&got[d],&got[v],&got[s]);
#endif
  assert(calls==expected);fast+=n-expected;
#ifdef __x86_64__
  assert(_mm_getcsr()==csr);
  /* The ordinary SoftFloat shortcut assumes host round-to-nearest. Its
   * independent reference calculation must not inherit our poisoned mode. */
  _mm_setcsr(0x1f80);
#endif
  for(unsigned k=0;k<n;k++) {if(width==32)ref[d].l[k]=float32_sqrt(ref[s].l[k],&y.sse_status);else ref[d].q[k]=float64_sqrt(ref[s].q[k],&y.sse_status);}
  if(kind>=2) {if(width==32)for(unsigned k=1;k<4;k++)ref[d].l[k]=ref[v].l[k];else ref[d].q[1]=ref[v].q[1];}
  if(memcmp(got,ref,sizeof(got))||memcmp(&x,&y,sizeof(x))) {fprintf(stderr,"sqrt mismatch mode%u kind%u alias%u pattern%u\n",mode,kind,alias,pattern);return 1;}
  cases++;
 }
#ifdef __x86_64__
 _mm_setcsr(0x1f80);
#endif
 printf("SSE native sqrt software=%u SHIFT=%u: %u exact result/status/alias/host-state cases; %u native lanes\n",SOFTWARE_ORACLE,SHIFT,cases,fast);
}

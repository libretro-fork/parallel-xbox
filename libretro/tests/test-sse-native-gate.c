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
static unsigned gates;
/* The full SoftFloat source's only GLib dependency, used for failed assertions. */
void g_assertion_message_expr(const char *domain,const char *file,int line,const char *func,const char *expr) { fprintf(stderr,"assertion %s:%d %s: %s\n",file,line,func,expr); abort(); }
/* PRODUCTION */
typedef void (*Fn)(CPUX86State *,TestReg *,TestReg *,TestReg *);
static Fn funcs[]={glue(helper_addps,SUFFIX),glue(helper_subps,SUFFIX),glue(helper_mulps,SUFFIX),glue(helper_divps,SUFFIX),glue(helper_minps,SUFFIX),glue(helper_maxps,SUFFIX),
 glue(helper_addpd,SUFFIX),glue(helper_subpd,SUFFIX),glue(helper_mulpd,SUFFIX),glue(helper_divpd,SUFFIX),glue(helper_minpd,SUFFIX),glue(helper_maxpd,SUFFIX),

#if SHIFT == 1
 helper_addss,helper_subss,helper_mulss,helper_divss,helper_minss,helper_maxss,
 helper_addsd,helper_subsd,helper_mulsd,helper_divsd,helper_minsd,helper_maxsd
#endif
};
static uint64_t rng=0xc145321ed789ab3;
static uint64_t random64(void) { rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng; }
#define REF_VALUE(size,member,hosttype) do { \
 float##size a=v->member[i],b=s->member[i],r; \
 if(op<3 && sse_native_ok_impl(env)) { \
  union { float##size s;hosttype h; } x={.s=a},y={.s=b},z; \
  switch(op) {case 0:z.h=x.h+y.h;break;case 1:z.h=x.h-y.h;break;default:z.h=x.h*y.h;break;}r=z.s; \
 } else {switch(op) {case 0:r=float##size##_add(a,b,&env->sse_status);break; \
 case 1:r=float##size##_sub(a,b,&env->sse_status);break;case 2:r=float##size##_mul(a,b,&env->sse_status);break; \
 case 3:r=float##size##_div(a,b,&env->sse_status);break;case 4:r=float##size##_lt(a,b,&env->sse_status)?a:b;break; \
 default:r=float##size##_lt(b,a,&env->sse_status)?a:b;break;} }d->member[i]=r; \
} while(0)
static void reference(CPUX86State *env,TestReg *d,TestReg *v,TestReg *s,unsigned which) {
 unsigned op=which%6,kind=which/6,n=kind==0?(2<<SHIFT):kind==1?(1<<SHIFT):1;
 for(unsigned i=0;i<n;i++) {if(kind==0||kind==2)REF_VALUE(32,l,float);else REF_VALUE(64,q,double);}
 if(kind>=2 && (!xemu_cpu_boost||d!=v)) {
  if(kind==2)for(unsigned i=1;i<4;i++)d->l[i]=v->l[i];
  else d->q[1]=v->q[1];
 }
}
int main(void) {
 static const unsigned alias[][3]={{0,1,2},{1,1,2},{2,1,2},{1,1,1}};
 static const uint64_t special[]={0,0x8000000080000000ULL,0x7fc123457f812345ULL,0x7ff8123456789abcULL,
 0x7ff0000000000001ULL,0x7f800000ff800000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,1,0x0010000000000000ULL};
 unsigned cases=0;
 for(unsigned mode=0;mode<16;mode++)for(unsigned boost=0;boost<2;boost++)
 for(unsigned host=0;host<4;host++)for(unsigned aliasno=0;aliasno<4;aliasno++)
 for(unsigned which=0;which<G_N_ELEMENTS(funcs);which++)for(unsigned pattern=0;pattern<24;pattern++) {
  TestReg a[3],b[3];for(unsigned j=0;j<3;j++)for(unsigned i=0;i<8;i++)a[j].q[i]=pattern<10?special[(pattern+i+j)%10]:random64();memcpy(b,a,sizeof(a));
  CPUX86State x={0},y;set_float_2nan_prop_rule(float_2nan_prop_x87,&x.sse_status);set_float_default_nan_pattern(0xc0,&x.sse_status);set_float_ftz_detection(float_ftz_after_rounding,&x.sse_status);x.sse_status.float_rounding_mode=mode&3;x.sse_status.flush_to_zero=!!(mode&4);x.sse_status.flush_inputs_to_zero=!!(mode&8);
  x.sse_status.float_exception_flags=pattern&0x1f;x.xemu_host_mxcsr=host==0||host==3?0:host==1?0x1f80:0x3f80;y=x;xemu_cpu_boost=boost;
  unsigned d=alias[aliasno][0],v=alias[aliasno][1],s=alias[aliasno][2];TestReg in_v=a[v],in_s=a[s];
  _mm_setcsr(host>=2?0x3f80:0x1f80);reference(&x,&a[d],&a[v],&a[s],which);unsigned mxcsr=_mm_getcsr();
  _mm_setcsr(host>=2?0x3f80:0x1f80);gates=0;funcs[which](&y,&b[d],&b[v],&b[s]);
  assert(gates==(which%6<3?1u:0u));
  /* The existing native path permits the compiler to commute add/mul.
   * For two NaNs, either input payload may win; both must be quieted.
   * Every other bit and all software results are compared exactly. */
  if(boost && mode==0 && host!=2 && (which%6==0 || which%6==2)) {
   unsigned kind=which/6,n=kind==0?(2<<SHIFT):kind==1?(1<<SHIFT):1;
   for(unsigned i=0;i<n;i++) {
    if(kind==0||kind==2) {
     uint32_t av=in_v.l[i],bv=in_s.l[i];
     if((av&0x7fffffff)>0x7f800000 && (bv&0x7fffffff)>0x7f800000) {
      uint32_t aq=av|0x400000,bq=bv|0x400000;
      assert((a[d].l[i]==aq||a[d].l[i]==bq) && (b[d].l[i]==aq||b[d].l[i]==bq));b[d].l[i]=a[d].l[i];
     }
    } else {
     uint64_t av=in_v.q[i],bv=in_s.q[i];
     if((av&0x7fffffffffffffffULL)>0x7ff0000000000000ULL && (bv&0x7fffffffffffffffULL)>0x7ff0000000000000ULL) {
      uint64_t aq=av|0x8000000000000ULL,bq=bv|0x8000000000000ULL;
      assert((a[d].q[i]==aq||a[d].q[i]==bq) && (b[d].q[i]==aq||b[d].q[i]==bq));b[d].q[i]=a[d].q[i];
     }
    }
   }
  }

  if(memcmp(a,b,sizeof(a))||memcmp(&x,&y,sizeof(x))||mxcsr!=_mm_getcsr()) {
   fprintf(stderr,"mismatch mode%u boost%u host%u alias%u op%u pattern%u\n",mode,boost,host,aliasno,which,pattern);for(unsigned j=0;j<3;j++)for(unsigned i=0;i<8;i++)if(a[j].q[i]!=b[j].q[i])fprintf(stderr,"reg%u lane%u %016llx != %016llx\n",j,i,(unsigned long long)a[j].q[i],(unsigned long long)b[j].q[i]);fprintf(stderr,"mxcsr %x %x statuscmp%d\n",mxcsr,_mm_getcsr(),memcmp(&x,&y,sizeof(x)));return 1;
  }
  cases++;
 }
 _mm_setcsr(0x1f80);printf("SSE native gate SHIFT=%u: %u arithmetic/status/alias cases passed\n",SHIFT,cases);
}

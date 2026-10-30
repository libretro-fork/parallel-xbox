/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/cpuid.h"
#include "host/include/i386/host/cpuinfo.h"
#include <xmmintrin.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif
void g_assertion_message_expr(const char *domain,const char *file,int line,const char *func,const char *expr) { fprintf(stderr,"assert %s:%d %s: %s\n",file,line,func,expr); abort(); }
static bool native_lahf(void) {
 unsigned a,b,c,d;
 if(__get_cpuid_max(0x80000000,0)<0x80000001)return false;
 __cpuid(0x80000001,a,b,c,d);return !!(c&bit_LAHF_LM);
}
static unsigned ext_max,ext_bits,ext_calls;
static unsigned fake_max(unsigned leaf,unsigned *sig) {
 assert(leaf==0||leaf==0x80000000);return leaf?ext_max:0;
}
static void fake_cpuid(unsigned leaf,unsigned *a,unsigned *b,unsigned *c,unsigned *d) {
 assert(leaf==0x80000001);ext_calls++;*a=*b=*d=0;*c=ext_bits;
}
#undef __get_cpuid_max
#undef __cpuid
#undef __cpuid_count
#define __get_cpuid_max fake_max
#define __cpuid(l,a,b,c,d) fake_cpuid(l,&a,&b,&c,&d)
#define __cpuid_count(l,n,a,b,c,d) fake_cpuid(l,&a,&b,&c,&d)
unsigned cpuinfo;
/* CPUINFO */
#define TCG_TARGET_REG_BITS TGC_TEST_BITS
#define TCG_REG_EAX 0
#define OPC_MOVZBL 0x1b6
#define OPC_PUSHF 0x9c
#define tcg_debug_assert assert
typedef unsigned TCGReg;
#define TCG_TYPE_I64 1
typedef struct { unsigned char bytes[128]; size_t n; } TCGContext;
static void tcg_out8(TCGContext *s,unsigned v) { assert(s->n<sizeof(s->bytes));s->bytes[s->n++]=v; }
static void tcg_out_modrm(TCGContext *s,unsigned op,unsigned r,unsigned m) {
 assert(op==OPC_MOVZBL&&r==0&&m==4);tcg_out8(s,0x0f);tcg_out8(s,0xb6);tcg_out8(s,0xc4);
}
static void tcg_out_pop(TCGContext *s,unsigned r) { if(r>=8)tcg_out8(s,0x41);tcg_out8(s,0x58+(r&7)); }
static void tcg_out_mov(TCGContext *s,unsigned type,unsigned dst,unsigned src) {
 assert(type==TCG_TYPE_I64&&src==0&&dst==10);tcg_out8(s,0x49);tcg_out8(s,0x89);tcg_out8(s,0xc2);
}
static bool tcg_out_xchg(TCGContext *s,unsigned type,unsigned a,unsigned b) {
 assert(type==TCG_TYPE_I64&&a==0&&b==10);tcg_out8(s,0x49);tcg_out8(s,0x87);tcg_out8(s,0xc2);return true;
}
/* EMITTER */
#define INDEX_op_com_f32 1
#define INDEX_op_com_f64 2
#define CONSTRAINT_a 1
#define CONSTRAINT_r 2
#define C_O1_I2(o,i,j) CONSTRAINT_##o
/* CONSTRAINT */
typedef uint64_t (__attribute__((sysv_abi)) *Compare)(uint64_t,uint64_t,uint64_t *);
static Compare make_compare(bool dp,bool lahf,unsigned dst) {
 TCGContext s={0};
 const unsigned char f32[]={0x66,0x0f,0x6e,0xc7,0x66,0x0f,0x6e,0xce,0x0f,0x2f,0xc1};
 const unsigned char f64[]={0x66,0x48,0x0f,0x6e,0xc7,0x66,0x48,0x0f,0x6e,0xce,0x66,0x0f,0x2f,0xc1};
 const unsigned char *prefix=dp?f64:f32;size_t n=dp?sizeof(f64):sizeof(f32);
 const unsigned char sentinel[]={0x48,0xb8,0xef,0xcd,0xab,0x89,0x67,0x45,0x23,0x01};
 for(size_t i=0;i<sizeof(sentinel);i++)tcg_out8(&s,sentinel[i]);
 for(size_t i=0;i<n;i++)tcg_out8(&s,prefix[i]);
 cpuinfo=lahf?CPUINFO_LAHF_LM:0;
 assert(constraint_for(dp?2:1)==CONSTRAINT_r);
 size_t capture=s.n;tcg_out_com_flags(&s,dst);
 const unsigned char reg0[]={0x9f,0x0f,0xb6,0xc4},reg10[]={0x49,0x89,0xc2,0x9f,0x0f,0xb6,0xc4,0x49,0x87,0xc2};
 const unsigned char stack0[]={0x9c,0x58},stack10[]={0x9c,0x41,0x5a};
 const unsigned char *expected=lahf&&TCG_TARGET_REG_BITS==64?(dst?reg10:reg0):(dst?stack10:stack0);
 size_t len=lahf&&TCG_TARGET_REG_BITS==64?(dst?sizeof(reg10):sizeof(reg0)):(dst?sizeof(stack10):sizeof(stack0));
 assert(s.n-capture==len&&!memcmp(s.bytes+capture,expected,len));
 /* Save RAX before moving the result into the ABI's return register. */
 tcg_out8(&s,0x48);tcg_out8(&s,0x89);tcg_out8(&s,0x02);
 if(dst){tcg_out8(&s,0x4c);tcg_out8(&s,0x89);tcg_out8(&s,0xd0);}
 tcg_out8(&s,0x83);tcg_out8(&s,0xe0);tcg_out8(&s,0x45);tcg_out8(&s,0xc3);
#ifdef _WIN32
 void *p=VirtualAlloc(0,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(p);memcpy(p,s.bytes,s.n);
 DWORD old;assert(VirtualProtect(p,4096,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),p,s.n);
#else
 void *p=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);memcpy(p,s.bytes,s.n);assert(!mprotect(p,4096,PROT_READ|PROT_EXEC));
#endif
 return (Compare)p;
}
static uint64_t random_bits(void) { static uint64_t x=0x91edcade12345678ULL;x^=x<<13;x^=x>>7;x^=x<<17;return x; }
int main(void) {
 unsigned maxes[]={0,0x80000000,0x80000001,0x80000008};
 for(unsigned m=0;m<4;m++)for(unsigned bits=0;bits<4;bits++) {
  ext_max=maxes[m];ext_bits=(bits&1?bit_LAHF_LM:0)|(bits&2?bit_LZCNT:0);ext_calls=cpuinfo=0;
  unsigned expected=CPUINFO_ALWAYS;if(m>=2)expected|=(bits&1?CPUINFO_LAHF_LM:0)|(bits&2?CPUINFO_LZCNT:0);
  assert(cpuinfo_init()==expected);assert(ext_calls==(m>=2));assert(cpuinfo_init()==expected);assert(ext_calls==(m>=2));
 }
 bool have=native_lahf();
 uint64_t patterns[]={0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x7f800000,0xff800000,0x7fc00001,0x7f800001,
  0x8000000000000000ULL,1,0x000fffffffffffffULL,0x0010000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000001ULL,0x7ff0000000000001ULL};
 unsigned cases=0;
 for(unsigned dp=0;dp<2;dp++)for(unsigned dest=0;dest<2;dest++) {
  unsigned dst=dest?10:0;Compare old=make_compare(dp,false,dst),now=make_compare(dp,have,dst);
  for(unsigned mode=0;mode<32;mode++)for(unsigned i=0;i<4096;i++) {
   uint64_t a=i<324?patterns[i/18]:random_bits(),b=i<324?patterns[i%18]:random_bits();
   unsigned csr=0x1f80|((mode&3)<<13)|((mode&4)?0x40:0)|((mode&8)?0x8000:0)|((mode&16)?0x3f:0);
   uint64_t old_rax,new_rax;_mm_setcsr(csr);uint64_t x=old(a,b,&old_rax);unsigned xf=_mm_getcsr();
   _mm_setcsr(csr);uint64_t y=now(a,b,&new_rax);unsigned yf=_mm_getcsr();
   if(dst)assert(old_rax==0x0123456789abcdefULL&&new_rax==old_rax);
   if(x!=y||xf!=yf){fprintf(stderr,"flag snapshot mismatch dp%u mode%u i%u\n",dp,mode,i);return 1;}cases++;
  }
 }
 _mm_setcsr(0x1f80);printf("FP flags: %u emitted comparisons/status cases, CPUID boundaries, constraints, target=%u LAHF=%u passed\n",cases,TCG_TARGET_REG_BITS,have);
 return 0;
}

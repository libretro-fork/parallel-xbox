/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PREFIX_DATA 1
#define PREFIX_VEX 2
#define MO_32 2
#ifndef HOST_BIG_ENDIAN
#define HOST_BIG_ENDIAN 0
#endif
typedef unsigned TCGv_i32;
typedef uint8_t *TCGv_ptr;
typedef void (*SSEFunc_0_pppi)(TCGv_ptr,TCGv_ptr,TCGv_ptr,TCGv_i32);
typedef struct { unsigned prefix,vex_l; } DisasContext;
typedef struct { unsigned offset;bool has_ea; } Op;
typedef struct { unsigned immediate;Op op[3]; } X86DecodedInsn;
static bool xemu_cpu_boost;
static uint8_t env[512];
static unsigned duplicates,helpers,kind;
static uint32_t rng=0xa91b42d3;
static uint32_t random32(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static unsigned tcg_constant_i32(unsigned n) { return n; }
static void shuffle(uint8_t *d,const uint8_t *v,const uint8_t *s,unsigned imm,unsigned bytes,bool pd) {
 uint8_t a[32],b[32],out[32];memcpy(a,v,bytes);memcpy(b,s,bytes);
 for(unsigned chunk=0;chunk<bytes;chunk+=16) {
  if(!pd) for(unsigned i=0;i<4;i++) memcpy(out+chunk+4*i,(i<2?a:b)+chunk+4*((imm>>(2*i))&3),4);
  else {unsigned shift=chunk/8;memcpy(out+chunk,a+chunk+8*((imm>>shift)&1),8);memcpy(out+chunk+8,b+chunk+8*((imm>>(shift+1))&1),8);}
 }
 memcpy(d,out,bytes);
}
static void shufps_xmm(TCGv_ptr d,TCGv_ptr v,TCGv_ptr s,unsigned n) { helpers++;kind=1;shuffle(d,v,s,n,16,false); }
static void shufpd_xmm(TCGv_ptr d,TCGv_ptr v,TCGv_ptr s,unsigned n) { helpers++;kind=2;shuffle(d,v,s,n,16,true); }
static void shufps_ymm(TCGv_ptr d,TCGv_ptr v,TCGv_ptr s,unsigned n) { helpers++;kind=3;shuffle(d,v,s,n,32,false); }
static void shufpd_ymm(TCGv_ptr d,TCGv_ptr v,TCGv_ptr s,unsigned n) { helpers++;kind=4;shuffle(d,v,s,n,32,true); }
#define gen_helper_shufps_xmm shufps_xmm
#define gen_helper_shufpd_xmm shufpd_xmm
#define gen_helper_shufps_ymm shufps_ymm
#define gen_helper_shufpd_ymm shufpd_ymm
#define OP_PTR0 (env+decode->op[0].offset)
#define OP_PTR1 (env+decode->op[1].offset)
#define OP_PTR2 (env+decode->op[2].offset)
static void tcg_gen_gvec_dup_mem(unsigned vece,unsigned d,unsigned a,unsigned size,unsigned max) {
 uint32_t value;assert(vece==MO_32 && size==16 && max==16);assert(a+4<=sizeof(env) && d+16<=sizeof(env));memcpy(&value,env+a,4);
 for(unsigned i=0;i<4;i++)memcpy(env+d+4*i,&value,4);
 duplicates++;
}
/* PRODUCTION */
int main(void) {
 static const unsigned offsets[][3]={{64,64,64},{64,64,128},{128,64,128},{192,64,64},{192,64,128},{64,64,256}};
 static const uint32_t special[]={0,0x80000000,0x7fc12345,0x7f812345,1,0x7f800000,0xff800000,0xffffffff};
 unsigned cases=0,hits=0;
 for(unsigned boost=0;boost<2;boost++)for(unsigned prefix=0;prefix<4;prefix++)
 for(unsigned wide=0;wide<2;wide++)for(unsigned alias=0;alias<6;alias++)
 for(unsigned imm=0;imm<256;imm++)for(unsigned pattern=0;pattern<4;pattern++) {
  for(unsigned i=0;i<sizeof(env);i+=4) {uint32_t x=pattern==0?special[(i/4)%8]:pattern==1?special[((i/4)+3)%8]:random32();memcpy(env+i,&x,4);}
  uint8_t reference[sizeof(env)];memcpy(reference,env,sizeof(env));
  DisasContext s={.prefix=prefix,.vex_l=wide};X86DecodedInsn decode={.immediate=imm};
  for(unsigned i=0;i<3;i++)decode.op[i].offset=offsets[alias][i];
  decode.op[2].has_ea=alias==5;xemu_cpu_boost=boost;duplicates=helpers=kind=0;
  shuffle(reference+offsets[alias][0],reference+offsets[alias][1],reference+offsets[alias][2],imm,wide?32:16,prefix&PREFIX_DATA);
  gen_VSHUF(&s,&decode);
  bool eligible=!HOST_BIG_ENDIAN && boost && !wide && !prefix && (alias==0 || alias==3) && (imm==0 || imm==0x55 || imm==0xaa || imm==0xff);
  assert(duplicates==(unsigned)eligible && helpers==(unsigned)!eligible);
  if(!eligible)assert(kind==((prefix&PREFIX_DATA)?2u:1u)+2*wide);
  assert(!memcmp(env,reference,sizeof(env)));
  cases++;hits+=eligible;
 }
 printf("SSE broadcast emission: %u bitwise/alias/encoding cases, %u fast-path cases passed\n",cases,hits);
}

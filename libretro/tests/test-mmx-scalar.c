/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef union {uint8_t b[8];uint16_t w[4];uint32_t l[2];uint64_t q[1];} MMXReg;
typedef struct {uint64_t guard[2];MMXReg regs[9];uint64_t status[4];} CPUX86State;
#define Reg MMXReg
#define SHIFT 0
#define SUFFIX _mmx
#define PACK_WIDTH 4
#define XMM_ONLY(...)
#define B(n) b[n]
#define W(n) w[n]
#define L(n) l[n]
#define Q(n) q[n]
#define glue_(a,b) a##b
#define glue(a,b) glue_(a,b)
/* ORACLE */
typedef uint64_t *TCGv_i64;
typedef int MemOp;
enum {MO_8,MO_16,MO_32,MO_64};
enum {PREFIX_DATA=1,PREFIX_VEX=2,X86_OP_MMX=3,X86_OP_SSE=4,X86_SPECIAL_MMX=5};
typedef struct {int offset,unit,ot;} X86DecodedOp;
typedef struct {X86DecodedOp op[3];struct {int special;} e;} X86DecodedInsn;
typedef struct {unsigned prefix,vex_l;} DisasContext;
typedef void (*SSEFunc_0_eppp)(CPUX86State *,MMXReg *,MMXReg *,MMXReg *);
static CPUX86State machine;
static CPUX86State *tcg_env=&machine;
static uint64_t temps[16];
static unsigned ntemp,loads,stores,fallback,illegal;
static TCGv_i64 tcg_temp_new_i64(void) {assert(ntemp<16);return &temps[ntemp++];}
static void tcg_gen_ld_i64(TCGv_i64 d,void *env,int ofs) {memcpy(d,(char *)env+ofs,8);loads++;}
static void tcg_gen_st_i64(TCGv_i64 s,void *env,int ofs) {memcpy((char *)env+ofs,s,8);stores++;}
static void tcg_gen_shri_i64(TCGv_i64 d,TCGv_i64 a,unsigned shift) {assert(shift<64);*d=*a>>shift;}
static void tcg_gen_deposit_i64(TCGv_i64 d,TCGv_i64 a,TCGv_i64 b,unsigned pos,unsigned width) {
    assert(width<=32 && pos+width<=64);uint64_t mask=((UINT64_C(1)<<width)-1)<<pos;
    *d=(*a&~mask)|((*b<<pos)&mask);
}
static void tcg_gen_sextract_i64(TCGv_i64 d,TCGv_i64 a,unsigned pos,unsigned width) {assert(width==16);*d=(int64_t)(int16_t)(*a>>pos);}
static void tcg_gen_mul_i64(TCGv_i64 d,TCGv_i64 a,TCGv_i64 b) {*d=*a**b;}
static int mmx_offset(int ot) {return 0;}
static int xmm_offset(int ot) {return 0;}
/* OFFSET */
#define OP_PTR0 ((MMXReg *)((char *)tcg_env+decode->op[0].offset))
#define OP_PTR1 ((MMXReg *)((char *)tcg_env+decode->op[1].offset))
#define OP_PTR2 ((MMXReg *)((char *)tcg_env+decode->op[2].offset))
static void gen_illegal_opcode(DisasContext *s) {illegal++;}
/* DISPATCH */
#define FALLBACK(name) \
static void gen_helper_##name##_mmx(CPUX86State *e,MMXReg *d,MMXReg *a,MMXReg *b) {fallback=1;helper_##name##_mmx(e,d,a,b);} \
static void gen_helper_##name##_xmm(CPUX86State *e,MMXReg *d,MMXReg *a,MMXReg *b) {fallback=2;} \
static void gen_helper_##name##_ymm(CPUX86State *e,MMXReg *d,MMXReg *a,MMXReg *b) {fallback=3;}
FALLBACK(punpcklbw) FALLBACK(punpcklwd) FALLBACK(punpckldq)
FALLBACK(punpckhbw) FALLBACK(punpckhwd) FALLBACK(punpckhdq) FALLBACK(pmulhw)
/* GENERATORS */
typedef void (*Gen)(DisasContext *,X86DecodedInsn *);
static Gen gens[]={gen_PUNPCKLBW,gen_PUNPCKLWD,gen_PUNPCKLDQ,gen_PUNPCKHBW,gen_PUNPCKHWD,gen_PUNPCKHDQ,gen_PMULHW};
static SSEFunc_0_eppp helpers[]={helper_punpcklbw_mmx,helper_punpcklwd_mmx,helper_punpckldq_mmx,helper_punpckhbw_mmx,helper_punpckhwd_mmx,helper_punpckhdq_mmx,helper_pmulhw_mmx};
static uint64_t rng=0x765bab31;
static uint64_t next(void) {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
int main(void) {
    unsigned cases=0;
    const uint64_t edges[]={0,UINT64_MAX,0x8000800080008000ull,0x7fff7fff7fff7fffull,0x0001000100010001ull,0x123456789abcdef0ull};
    for(unsigned trial=0;trial<200000;trial++) for(unsigned alias=0;alias<6;alias++) {
        unsigned dest=alias==1?1:0,first=alias==2?2:0,second=alias==3?0:alias==4?8:1;
        memset(&machine,0xa5,sizeof(machine));
        machine.regs[first].q[0]=trial<36?edges[trial%6]:next();
        machine.regs[second].q[0]=trial<36?edges[trial/6]:next();
        for(unsigned op=0;op<7;op++) {
            CPUX86State before=machine,expected=machine;
            helpers[op](&expected,&expected.regs[dest],&expected.regs[first],&expected.regs[second]);
            X86DecodedInsn decode={.e.special=X86_SPECIAL_MMX};
            unsigned indexes[]={dest,first,second};
            for(unsigned i=0;i<3;i++) decode.op[i]=(X86DecodedOp){.offset=offsetof(CPUX86State,regs)+8*indexes[i],.unit=X86_OP_MMX,.ot=MO_64};
            DisasContext s={0};ntemp=loads=stores=fallback=illegal=0;
            gens[op](&s,&decode);
            assert(!memcmp(&machine,&expected,sizeof(machine)));
            assert(loads==2 && stores==1 && !fallback && !illegal);
            machine=before;cases++;
        }
    }
    for(unsigned prefix=1;prefix<4;prefix++) for(unsigned wide=0;wide<2;wide++) for(unsigned op=0;op<7;op++) {
        X86DecodedInsn decode={.e.special=X86_SPECIAL_MMX};
        DisasContext s={.prefix=prefix,.vex_l=wide};
        CPUX86State before=machine;ntemp=loads=stores=fallback=illegal=0;
        gens[op](&s,&decode);
        assert(!memcmp(&before,&machine,sizeof(machine)) && !loads && !stores);
        if(prefix==PREFIX_VEX) assert(illegal==1 && !fallback);
        else assert(!illegal && fallback==(wide?3:2));
    }
    printf("PASS: %u production MMX generator/helper comparisons; signed edges, aliases, memory scratch, unchanged state, SSE/VEX dispatch\n",cases);
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum {MO_8,MO_16,MO_32,MO_64};
typedef unsigned MemOp;
typedef uint32_t *TCGv;
typedef uint64_t *TCGv_i64;
typedef struct {unsigned ot,offset;} Op;
typedef struct {Op op[3];} X86DecodedInsn;
typedef struct {unsigned length;TCGv T1;} DisasContext;
static unsigned char state[512];
static void *tcg_env=state;
static uint64_t temp;
static unsigned stores,clears;
static int vector_len(DisasContext *s,X86DecodedInsn *d) {return s->length;}
static int vector_elem_offset(Op *op,MemOp ot,int n) {return op->offset;}
static TCGv_i64 tcg_temp_new_i64(void) {return &temp;}
static void tcg_gen_extu_tl_i64(TCGv_i64 d,TCGv s) {*d=*s;}
static void tcg_gen_st_i64(TCGv_i64 s,void *env,unsigned offset) {memcpy((char *)env+offset,s,8);stores++;}
static void tcg_gen_st_tl(TCGv s,void *env,unsigned offset) {memcpy((char *)env+offset,s,4);stores++;}
static void tcg_gen_gvec_dup_imm(unsigned ot,unsigned offset,unsigned size,unsigned maxsize,uint64_t v) {assert(!v && size==maxsize);memset(state+offset,0,size);stores++;clears++;}
#define g_assert_not_reached() assert(false)
/* GENERATOR */
int main(void) {
    const unsigned lengths[]={8,16,32,64};
    const uint32_t edges[]={0,1,0x7fffffff,0x80000000,0xffffffff,0x01234567,0xdeadbeef};
    uint32_t rng=0x77654;unsigned cases=0;
    for(unsigned trial=0;trial<100000;trial++) for(unsigned len=0;len<4;len++)
    for(unsigned reg=0;reg<8;reg++) {
        rng=rng*1664525u+1013904223u;
        uint32_t v=trial<7?edges[trial]:rng;
        DisasContext s={.length=lengths[len],.T1=&v};
        X86DecodedInsn d={.op={{.offset=64+reg*8},{0},{.ot=MO_32}}};
        memset(state,0xa5,sizeof(state));unsigned char expected[sizeof(state)];memcpy(expected,state,sizeof(state));
        memset(expected+d.op[0].offset,0,s.length);memcpy(expected+d.op[0].offset,&v,4);
        stores=clears=0;gen_MOVD_to(&s,&d);
        assert(!memcmp(state,expected,sizeof(state)));
        assert(stores==(len?2:1) && clears==(len?1:0));
        cases++;
    }
    printf("PASS: %u MOVD value/register/vector-width cases; one MMX store, zero extension, untouched neighbors and SSE/AVX clearing\n",cases);
}

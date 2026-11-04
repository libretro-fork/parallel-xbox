/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tcg/debug-assert.h"
typedef unsigned TCGType;
typedef struct { TCGType base_type; uint64_t lane[32]; } Vector;
typedef Vector *TCGv_vec;
enum { INDEX_op_abs_vec, INDEX_op_sub_vec, INDEX_op_smax_vec, INDEX_op_sari_vec };
enum { TCG_COND_LT };
static Vector temporary, zero;
static int caps[4];
static unsigned queries[4], allocations, frees;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "ABS: %u: %s\n", __LINE__, #x); exit(1); } } while (0)
#define tcgv_vec_temp(v) (v)
static uint64_t mask(unsigned e) { return UINT64_MAX >> (64 - (8u << e)); }
static uint64_t sign(unsigned e) { return UINT64_C(1) << ((8u << e) - 1); }
static unsigned lanes(TCGv_vec v, unsigned e) { return v->base_type / (8u << e); }
static int tcg_can_emit_vec_op(unsigned op, TCGType type, unsigned e)
{ (void)type; (void)e; ++queries[op]; return caps[op]; }
static TCGv_vec tcg_temp_new_vec(TCGType type)
{ ++allocations; temporary.base_type = type; return &temporary; }
static void tcg_temp_free_vec(TCGv_vec v) { CHECK(v == &temporary); ++frees; }
static TCGv_vec tcg_constant_vec(TCGType type, unsigned e, uint64_t value)
{ (void)e; CHECK(value == 0); zero.base_type = type; return &zero; }
static bool do_op2(unsigned e, TCGv_vec r, TCGv_vec a, unsigned op)
{
    if (!tcg_can_emit_vec_op(op, r->base_type, e)) return false;
    for (unsigned i=0; i<lanes(r,e); ++i) {
        uint64_t x=a->lane[i]; r->lane[i]=(x & sign(e)) ? (0-x)&mask(e) : x;
    }
    return true;
}
static void tcg_gen_neg_vec(unsigned e, TCGv_vec r, TCGv_vec a)
{ for (unsigned i=0;i<lanes(r,e);++i) r->lane[i]=(0-a->lane[i])&mask(e); }
static void tcg_gen_smax_vec(unsigned e, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{ for (unsigned i=0;i<lanes(r,e);++i) r->lane[i]=(a->lane[i]^sign(e))>(b->lane[i]^sign(e)) ? a->lane[i]:b->lane[i]; }
static void tcg_gen_sari_vec(unsigned e, TCGv_vec r, TCGv_vec a, unsigned n)
{ CHECK(n == (8u<<e)-1); for(unsigned i=0;i<lanes(r,e);++i) r->lane[i]=(a->lane[i]&sign(e))?mask(e):0; }
static void tcg_gen_cmp_vec(unsigned cond, unsigned e, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{ CHECK(cond==TCG_COND_LT && b==&zero); for(unsigned i=0;i<lanes(r,e);++i) r->lane[i]=(a->lane[i]&sign(e))?mask(e):0; }
static void tcg_gen_xor_vec(unsigned e, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{ (void)e; for(unsigned i=0;i<r->base_type/8;++i) r->lane[i]=a->lane[i]^b->lane[i]; }
static void tcg_gen_sub_vec(unsigned e, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{ for(unsigned i=0;i<lanes(r,e);++i) r->lane[i]=(a->lane[i]-b->lane[i])&mask(e); }
void tcg_gen_abs_vec(unsigned vece, TCGv_vec r, TCGv_vec a);
/* PRODUCTION */
int main(void)
{
    unsigned cases=0;
    for(unsigned width=64;width<=256;width*=2) for(unsigned e=0;e<4;++e)
    for(int mode=0;mode<5;++mode) for(unsigned alias=0;alias<2;++alias)
    for(int sub=-1;sub<=1;sub+=2) for(unsigned pattern=0;pattern<8;++pattern) {
        Vector a={.base_type=width}, out={.base_type=width};
        uint64_t expected[32];
        uint64_t values[]={0,1,mask(e),sign(e),sign(e)-1,sign(e)+1,mask(e)-1,42};
        for(unsigned i=0;i<lanes(&a,e);++i) {
            uint64_t x=values[(i+pattern)%8]&mask(e); a.lane[i]=x;
            /* Independent unsigned magnitude oracle, including signed minimum. */
            expected[i]=x<sign(e)?x:((~x)&mask(e))+1;
            expected[i]&=mask(e);
        }
        for(unsigned i=0;i<4;++i) queries[i]=0;
        allocations=frees=0;
        caps[0]=mode<2 ? (mode ? -1:1):0;
        caps[1]=sub; caps[2]=mode==2?1:0; caps[3]=mode==3?1:0;
        TCGv_vec result=alias?&a:&out;
        tcg_gen_abs_vec(e,result,&a);
        for(unsigned i=0;i<lanes(result,e);++i) CHECK(result->lane[i]==expected[i]);
        CHECK(queries[0]==1 && allocations==(unsigned)(mode>=2) && frees==allocations);
        CHECK(queries[2]==(unsigned)(mode>=2));
        CHECK(queries[3]==(unsigned)(mode>=3));
#if defined(CONFIG_DEBUG_TCG) || defined(QEMU_STATIC_ANALYSIS)
        CHECK(queries[1]==(unsigned)(mode>=2));
#else
        CHECK(queries[1]==0);
#endif
        ++cases;
    }
    printf("vector ABS: %u cases passed\n",cases);
    return 0;
}

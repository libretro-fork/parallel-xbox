/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint64_t low;uint16_t high;uint8_t pad[6];} Image;
typedef Image floatx80;
typedef union {Image d;uint64_t mmx;} FPReg;
typedef struct {uint32_t fpstt;uint8_t tags[8];FPReg fpregs[8];uint64_t status[8];} CPUX86State;
typedef struct {bool keep;} DisasContext;
typedef uint32_t *TCGv_i32,*TCGv;
typedef uint64_t *TCGv_i64;
typedef uintptr_t *TCGv_ptr;
static CPUX86State machine;
static uintptr_t env=(uintptr_t)&machine;
static TCGv_ptr tcg_env=&env;
static uint32_t *fpstt=&machine.fpstt;
static bool g_use_hard_fpu;
static union {uint64_t q;uint32_t l;uintptr_t p;} pool[64];
static unsigned used,loads,stores,predicates;
static void *temp(void) {assert(used<64);return &pool[used++];}
static TCGv tcg_temp_new(void) {return temp();}
static TCGv_i32 tcg_temp_new_i32(void) {return temp();}
static TCGv_i64 tcg_temp_new_i64(void) {return temp();}
static TCGv_ptr tcg_temp_new_ptr(void) {return temp();}
static TCGv_i32 tcg_constant_i32(uint32_t v) {TCGv_i32 p=temp();*p=v;return p;}
static TCGv_i64 tcg_constant_i64(uint64_t v) {TCGv_i64 p=temp();*p=v;return p;}
static void tcg_gen_mov_i32(TCGv_i32 d,TCGv_i32 s) {*d=*s;}
static void tcg_gen_addi_i32(TCGv_i32 d,TCGv_i32 s,uint32_t v) {*d=*s+v;}
static void tcg_gen_andi_i32(TCGv_i32 d,TCGv_i32 s,uint32_t v) {*d=*s&v;}
static void tcg_gen_muli_i32(TCGv_i32 d,TCGv_i32 s,uint32_t v) {*d=*s*v;}
static void tcg_gen_ext_i32_ptr(TCGv_ptr d,TCGv_i32 s) {*d=(int32_t)*s;}
static void tcg_gen_add_ptr(TCGv_ptr d,TCGv_ptr a,TCGv_ptr b) {*d=*a+*b;}
static void gen_setcc(DisasContext *s,int b,TCGv d) {*d=s->keep;predicates++;}
static void tcg_gen_extu_tl_i64(TCGv_i64 d,TCGv s) {*d=*s;}
static void tcg_gen_trunc_tl_i32(TCGv_i32 d,TCGv s) {*d=*s;}
static void tcg_gen_ld_i64(TCGv_i64 d,TCGv_ptr p,unsigned o) {memcpy(d,(void *)(*p+o),8);loads++;}
static void tcg_gen_ld16u_i32(TCGv_i32 d,TCGv_ptr p,unsigned o) {uint16_t v;memcpy(&v,(void *)(*p+o),2);*d=v;loads++;}
enum {TCG_COND_NE};
static bool test64(int c,TCGv_i64 a,TCGv_i64 b) {assert(c==TCG_COND_NE);return *a!=*b;}
static void tcg_gen_movcond_i64(int c,TCGv_i64 d,TCGv_i64 a,TCGv_i64 b,TCGv_i64 v1,TCGv_i64 v2) {*d=test64(c,a,b)?*v1:*v2;}
static void tcg_gen_movcond_i32(int c,TCGv_i32 d,TCGv_i32 a,TCGv_i32 b,TCGv_i32 v1,TCGv_i32 v2) {assert(c==TCG_COND_NE);*d=*a!=*b?*v1:*v2;}
static void tcg_gen_st_i64(TCGv_i64 s,TCGv_ptr p,unsigned o) {memcpy((void *)(*p+o),s,8);stores++;}
static void tcg_gen_st16_i32(TCGv_i32 s,TCGv_ptr p,unsigned o) {uint16_t v=*s;memcpy((void *)(*p+o),&v,2);stores++;}
/* POINTER */
/* SELECT */
#define ST0 (env->fpregs[env->fpstt].d)
#define ST(n) (env->fpregs[(env->fpstt+(n))&7].d)
/* HELPER */
static uint64_t rng=0x761994;
static uint64_t next(void) {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
int main(void) {
    static const Image edges[]={{0,0},{0,0x8000},{1,0},{0x8000000000000000ull,0x7fff},
        {0x8000000000000001ull,0x7fff},{0xc123456789abcdefull,0xffff},{1,0x3fff},
        {0x8123456789abcdefull,0x7ffe},{0x8123456789abcdefull,1}};
    unsigned cases=0;
    for(unsigned trial=0;trial<10000;trial++)for(unsigned index=0;index<8;index++)for(unsigned keep=0;keep<2;keep++) {
        memset(&machine,0xa5,sizeof(machine));machine.fpstt=trial&7;
        for(unsigned r=0;r<8;r++) {
            machine.fpregs[r].d.low=next();machine.fpregs[r].d.high=next();
            if(trial<81) {Image e=edges[(trial+r)%9];machine.fpregs[r].d.low=e.low;machine.fpregs[r].d.high=e.high;}
            machine.tags[r]=next()&1;machine.status[r]=next();
        }
        CPUX86State before=machine,expected=machine;
        if(!keep)helper_fmov_ST0_STN(&expected,index);
        DisasContext s={.keep=keep};used=loads=stores=predicates=0;g_use_hard_fpu=false;
        assert(gen_fcmov_soft(&s,trial%16,index));
        for(unsigned r=0;r<8;r++)assert(machine.fpregs[r].d.low==expected.fpregs[r].d.low&&machine.fpregs[r].d.high==expected.fpregs[r].d.high);
        assert(!memcmp(machine.tags,before.tags,sizeof(machine.tags))&&!memcmp(machine.status,before.status,sizeof(machine.status)));
        assert(machine.fpstt==before.fpstt&&loads==4&&stores==2&&predicates==1);
        machine=before;g_use_hard_fpu=true;used=loads=stores=predicates=0;
        assert(!gen_fcmov_soft(&s,trial%16,index));
        assert(!memcmp(&machine,&before,sizeof(machine))&&!used&&!loads&&!stores&&!predicates);
        cases++;
    }
    printf("PASS: %u exact 80-bit FCMOV/helper cases; all TOP/aliases, raw special encodings, preserved state and untouched hard-FPU fallback\n",cases);
}

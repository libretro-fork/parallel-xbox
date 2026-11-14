/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef struct { uint32_t before, vsh_constants[192][4], reg, after;
                 bool vsh_constants_dirty[192]; } PGRAPHState;
typedef int NV2AState;
#define PG_GET_MASK(r,m) ((pg->reg & (m)) / ((m)&-(m)))
#define PG_SET_MASK(r,m,v) (pg->reg=(pg->reg & ~(m)) | (((v)*((m)&-(m)))&(m)))
#define DEF_METHOD(g,n) static void upload(PGRAPHState *pg, unsigned subchannel __attribute__((unused)), unsigned method, uint32_t parameter, uint32_t *parameters, size_t num_words_available, size_t *num_words_consumed, bool inc)
#define METHOD_RANGE_END_NAME(g,n) (NV097_SET_TRANSFORM_CONSTANT+32*4)
#define pgraph_method_log(...) ((void)0) /* Legacy candidates only. */
static uint32_t ldl_le_p(const void *p)
{ const unsigned char *b=p;return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24; }
/* PRODUCTION */
/* Frozen pre-backport word behavior and incremental dispatch. */
static void reference(PGRAPHState *pg,unsigned sub,unsigned method,uint32_t parameter,
                      uint32_t *parameters,size_t available,size_t *consumed,bool inc)
{
    (void)sub;
    size_t count=inc ? MIN(available,(NV097_SET_TRANSFORM_CONSTANT+128-method)/4) : 1;
    for(size_t i=0;i<count;i++) {
        if(inc)parameter=ldl_le_p((unsigned char *)parameters+4*i);
        unsigned slot=(method-NV097_SET_TRANSFORM_CONSTANT)/4;
        unsigned row=PG_GET_MASK(NV_PGRAPH_CHEOPS_OFFSET,NV_PGRAPH_CHEOPS_OFFSET_CONST_LD_PTR);
        assert(row<192);
        pg->vsh_constants_dirty[row]|=parameter!=pg->vsh_constants[row][slot%4];
        pg->vsh_constants[row][slot%4]=parameter;
        if(slot%4==3)PG_SET_MASK(NV_PGRAPH_CHEOPS_OFFSET,NV_PGRAPH_CHEOPS_OFFSET_CONST_LD_PTR,row+1);
        if(inc)method+=4;
    }
    if(inc)*consumed=count;
}
static uint64_t seed=0x12345;
static uint32_t rnd(void) { seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed; }
int main(void)
{
    unsigned cases=0;
    for(unsigned trial=0;trial<100;trial++)
    for(unsigned slot=0;slot<32;slot++)
    for(unsigned available=0;available<=40;available++)
    for(unsigned inc=0;inc<2;inc++) {
        PGRAPHState pg={0},expected;
        for(unsigned row=0;row<192;row++) {
            pg.vsh_constants_dirty[row]=(rnd()&7)==0;
            for(unsigned k=0;k<4;k++)pg.vsh_constants[row][k]=rnd();
        }
        pg.before=pg.after=0xabcdef12;
        unsigned row=trial%5 ? rnd()%184 : 184;
        const uint32_t mask=NV_PGRAPH_CHEOPS_OFFSET_CONST_LD_PTR;
        pg.reg=(rnd()&~mask)|((row*(mask&-mask))&mask);
        unsigned char buffer[164];unsigned offset=trial%4;
        for(unsigned i=0;i<40;i++) {
            uint32_t value=trial%3 ? rnd() : pg.vsh_constants[(row+(slot%4+i)/4)%192][(slot+i)%4];
            for(unsigned b=0;b<4;b++)buffer[offset+4*i+b]=value>>(8*b);
        }
        /* Unaligned DMA payload, partial rows, repeated values and dirty bits. */
        expected=pg;size_t actual_count=1,expected_count=1;
        unsigned method=NV097_SET_TRANSFORM_CONSTANT+4*slot;
        uint32_t parameter=rnd();
        reference(&expected,3,method,parameter,(void *)(buffer+offset),available,&expected_count,inc);
        upload(&pg,3,method,parameter,(void *)(buffer+offset),available,&actual_count,inc);
        assert(actual_count==expected_count);
        assert(!memcmp(&pg,&expected,sizeof(pg)));
        cases++;
    }
    printf("PASS: %u constant uploads match word handler: bytes, dirty rows, register bits, consumption\n",cases);
}

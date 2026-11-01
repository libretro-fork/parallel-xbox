/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
enum {NV_PFIFO_CACHE1_DMA_GET,NV_PFIFO_CACHE1_DMA_STATE,NV_PFIFO_CACHE1_DMA_DCOUNT};
enum {NV_PFIFO_CACHE1_DMA_STATE_METHOD,NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT};
#define NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE_INC 0
struct Device {uint32_t get,method,count,dcount;unsigned calls,writes;ssize_t result;};
static uint32_t pfifo_reg_r(struct Device*d,unsigned reg){assert(reg==NV_PFIFO_CACHE1_DMA_DCOUNT);return d->dcount;}
static void pfifo_reg_w(struct Device*d,unsigned reg,uint32_t value){assert(reg==NV_PFIFO_CACHE1_DMA_GET||reg==NV_PFIFO_CACHE1_DMA_DCOUNT);if(reg==NV_PFIFO_CACHE1_DMA_GET)d->get=value;else d->dcount=value;d->writes++;}
static void set(struct Device*d,unsigned field,uint32_t value){if(field==NV_PFIFO_CACHE1_DMA_STATE_METHOD)d->method=value;else d->count=value;d->writes++;}
#define PFIFO_REG_SET_MASK(d,reg,field,value) set(d,field,value)
static ssize_t pfifo_run_puller(struct Device*d,uint32_t entry,uint32_t word,uint32_t*ptr,size_t available,size_t lookahead){d->calls++;return d->result;}
static void run(struct Device*d,unsigned method_type)
{
    for(unsigned pass=0;pass<4;pass++) {
        uint32_t dma_get_v=d->get+4;
        uint32_t method=d->method<<2,method_count=d->count;
        uint32_t method_entry=0,word=0,*word_ptr=&word;
        size_t num_words_available=d->count;
/* PRODUCTION */
        pfifo_reg_w(d,NV_PFIFO_CACHE1_DMA_GET,dma_get_v);
        if(d->result>0)break;
    }
}
int main(void)
{
 unsigned cases=0;
 for(unsigned inc=0;inc<2;inc++)
 for(unsigned count=1;count<=64;count++)
 for(int n=-1;n<=(int)count;n++) {
    struct Device d={.get=0x100,.method=0x40,.count=count,.dcount=7,.result=n};
    run(&d,inc);
    assert(d.calls==1);
    assert(d.get==(n>0?0x100+4*(unsigned)n:0x100));
    assert(d.count==(n>0?count-(unsigned)n:count));
    assert(d.dcount==(n>0?7+(unsigned)n:7));
    assert(d.method==(n>0&&!inc?0x40+(unsigned)n:0x40));
    assert(d.writes==(n>0?(inc?3u:4u):0u));
    if(n==0){d.result=1;run(&d,inc);assert(d.calls==2&&d.get==0x104&&d.count==count-1);}
    cases++;
 }
 printf("PASS: %u PFIFO no-progress/retry/accounting cases\n",cases);
 return 0;
}

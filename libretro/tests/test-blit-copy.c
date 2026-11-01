/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;
static void *copy(void *d,const void *s,size_t n) { calls++;return memmove(d,s,n); }
#define memmove copy
#include "hw/xbox/nv2a/pgraph/copy.h"
#undef memmove
int main(void)
{
    uint8_t actual[512], expected[512];
    unsigned cases=0, coalesced=0;
    for(unsigned src=0;src<17;src++)
    for(unsigned dst=0;dst<160;dst+=3)
    for(unsigned width=0;width<=16;width++)
    for(unsigned height=0;height<=8;height++)
    for(unsigned layout=0;layout<5;layout++) {
        size_t sp=layout==1?0:width+(layout==2?3:0);
        size_t dp=layout==3?0:width+(layout==4?5:0);
        for(unsigned i=0;i<sizeof(actual);i++) actual[i]=expected[i]=(i*37+cases)%251;
        for(unsigned y=0;y<height;y++)
            memmove(expected+dst+y*dp,expected+src+y*sp,width);
        calls=0;
        pgraph_copy_rows(actual+dst,actual+src,width,height,sp,dp);
        assert(!memcmp(actual,expected,sizeof(actual)));
        unsigned distance=src<dst?dst-src:src-dst;
        bool batch=width && height && sp==width && dp==width && distance>=width*height;
        assert(calls==(!width||!height?0:batch?1:height));
        if(batch && height>1) coalesced++;
        cases++;
    }
    /* Empty copies do not even form pointers, regardless of guest counts. */
    calls=0;pgraph_copy_rows(NULL,NULL,0,SIZE_MAX,SIZE_MAX,SIZE_MAX);
    pgraph_copy_rows(NULL,NULL,SIZE_MAX,0,SIZE_MAX,SIZE_MAX);assert(!calls);
    printf("PASS: %u row-copy cases; %u multi-row copies coalesced\n",cases,coalesced);
    return 0;
}

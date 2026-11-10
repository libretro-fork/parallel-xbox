/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"constant lend line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static volatile unsigned allocations;
static void *test_allocate(size_t n){allocations++;return malloc(n);}
#define malloc test_allocate
/* PRODUCTION */
#undef malloc

int main(void)
{
    unsigned cases=0;
    for(unsigned iteration=0;iteration<10000;iteration++)for(unsigned relative=0;relative<2;relative++){
        uint32_t words[256];
        Writer w={.words=words,.capacity=256,.next=1024,.block=700};
        Context c={.w=&w,.ubo=10,.uniform_v4=11,.v4=12,.i=13,.a0=42};
        for(unsigned i=0;i<256;i++)c.ci[i]=100+i;
        unsigned selector=iteration%192;
        uint32_t t[4]={0,selector<<13|0x1b,
            PARAM_C<<26|PARAM_C<<11|0x1bu<<17|0x1bu<<2,
            PARAM_C<<28|(relative?2:0)};
        unsigned cost=relative?15:10;
        for(unsigned arg=0;arg<3;arg++){
            size_t before=w.count;
            operand(&c,t,arg);
            CHECK(w.count-before==(arg?9:cost+9));
        }
        CHECK(w.count==cost+27); /* prior path: 3*cost+27 */
        size_t before=w.count;
        uint32_t held=constant_read(&c,selector,relative);
        CHECK(w.count==before&&held==c.constant_value);
        w.block++;
        CHECK(constant_read(&c,selector,relative)!=held&&w.count==before+cost);
        before=w.count;held=c.constant_value;
        c.a0++;
        uint32_t after_address=constant_read(&c,selector,relative);
        CHECK(w.count==before+(relative?cost:0));
        CHECK(relative?(after_address!=held):(after_address==held));
        before=w.count;
        CHECK(constant_read(&c,(selector+1)%192,relative)!=after_address);
        CHECK(w.count==before+cost);
        before=w.count;
        held=c.constant_value;
        CHECK(constant_read(&c,selector,!relative)!=held);
        CHECK(w.count==before+(relative?10:15));
        CHECK(!allocations);
        cases++;
    }
    CHECK(cases==20000);
    puts("Constant lends: 20000 operand/selector/address/block/scope cases PASS; 3 constant operands 57->37 direct or 72->42 relative words; zero heap calls, 16 metadata bytes");
    return 0;
}

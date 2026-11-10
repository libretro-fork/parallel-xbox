/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"integer lend line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
/* PRODUCTION */
int main(void)
{
    unsigned cases=0;
    for(unsigned generation=0;generation<1024;generation++){
        Writer count={0};Context c={.w=&count};
        for(unsigned i=0;i<256;i++)c.ci[i]=1024+i;
        for(unsigned i=0;i<256;i++){
            unsigned index=(i*197+generation)%256;
            CHECK(integer_id(&c,index)==1024+index);
            CHECK(count.count==(i+1)*4);
            CHECK(integer_id(&c,index)==1024+index);
            CHECK(count.count==(i+1)*4);
            cases++;
        }
        for(unsigned i=0;i<4;i++)CHECK(count.integer_used[i]==UINT64_MAX);
        uint32_t words[1]={0xdeadbeef};
        Writer output={.words=words,.integer_live=count.integer_used};c.w=&output;
        for(unsigned i=0;i<256;i++)CHECK(integer_id(&c,i)==1024+i);
        CHECK(!output.count && words[0]==0xdeadbeef);
        CHECK(!memcmp(output.integer_used,count.integer_used,sizeof(count.integer_used)));
    }
    /* Both emit passes are synchronous borrowers of the immutable key. */
    VshState s={.is_fixed_function=true};
    SpvVshOptions opts={.preserve_float32=true,.ubo_binding=1};
    Writer count={.next=1};emit(&count,&s,opts);
    uint32_t words[8192];CHECK(count.count<8192);
    Writer output={.words=words,.capacity=count.count,.next=1,
        .precise_mul={count.precise_mul[0],count.precise_mul[1],count.precise_mul[2],count.precise_mul[3]},
        .precise_distance=count.precise_distance,.integer_live=count.integer_used};
    emit(&output,&s,opts);
    CHECK(output.count==count.count && output.next==count.next);
    CHECK(!memcmp(output.integer_used,count.integer_used,sizeof(count.integer_used)));
    printf("Integer lends: %u unique/repeat/index/generation cases; exact count/write ownership PASS\n",cases);
}

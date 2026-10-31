/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_vsh_emulator.h"
static unsigned decodes, allocations;
static bool watch_allocations;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
void *__wrap_malloc(size_t n) { allocations += watch_allocations; return __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t s) { allocations += watch_allocations; return __real_calloc(n,s); }
void *__wrap_realloc(void *p,size_t n) { allocations += watch_allocations; return __real_realloc(p,n); }
static Nv2aVshParseResult counted_parse(Nv2aVshStep *step,const uint32_t *words)
{ decodes++; return nv2a_vsh_parse_step(step,words); }
#define nv2a_vsh_parse_step counted_parse
#include "hw/xbox/nv2a/pgraph/vsh-state.h"
#undef nv2a_vsh_parse_step
typedef struct {
    PGRAPHVshStateCache vertex_state_program_cache;
    uint32_t program_data[136][4],vertex_state_shader_v0[4],vsh_constants[192][4];
    bool vsh_constants_dirty[192];
} PGRAPHState;
/* HANDLER */

static unsigned comparisons;
static uint32_t random_state=381;
static uint32_t random_value(void) { random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
static void instruction(uint32_t *raw,unsigned index,bool final)
{
    raw[0]=random_value(); /* ignored word must still be part of the key */
    raw[1]=(4u<<21)|((index%192)<<13)|(random_value()&511);
    raw[2]=(3u<<26)|(2u<<11)|(random_value()&((255u<<17)|(1u<<25)|(255u<<2)|(1u<<10)));
    raw[3]=(2u<<28)|((1+random_value()%15)<<12)|((random_value()%192)<<3)|final;
}
static void execute(Nv2aVshProgram *program,float *constants,bool *dirty,const float input[4])
{
    Nv2aVshCPUXVSSExecutionState linkage;
    Nv2aVshExecutionState state=nv2a_vsh_emu_initialize_xss_execution_state(&linkage,constants);
    memcpy(linkage.input_regs,input,16);
    nv2a_vsh_emu_execute_track_context_writes(&state,program,dirty);
}
static void parity(PGRAPHVshStateCache *cache,uint32_t words[][4],unsigned start,unsigned length)
{
    Nv2aVshProgram reference,cached;
    assert(nv2a_vsh_parse_program(&reference,words[start],length)==NV2AVPR_SUCCESS);
    float a[192*4],b[192*4],input[4];bool da[192],db[192];
    for(unsigned i=0;i<192*4;i++) a[i]=b[i]=(int)(random_value()%2001)/1000.f-1;
    for(unsigned i=0;i<192;i++) da[i]=db[i]=(random_value()%7==0);
    for(unsigned i=0;i<4;i++) input[i]=(int)(random_value()%2001)/1000.f-1;
    execute(&reference,a,da,input);
    unsigned before=allocations;watch_allocations=true;
    assert(pgraph_vsh_state_program(cache,(const uint32_t (*)[4])words,start,&cached)==NV2AVPR_SUCCESS);
    execute(&cached,b,db,input);watch_allocations=false;
    assert(allocations==before && !memcmp(a,b,sizeof(a)) && !memcmp(da,db,sizeof(da)));
    nv2a_vsh_program_destroy(&reference);
    comparisons++;
}
int main(void)
{
    static PGRAPHVshStateCache cache;
    uint32_t words[136][4];memset(words,0xff,sizeof(words));
    instruction(words[0],0,true);Nv2aVshProgram program;
    assert(pgraph_vsh_state_program(&cache,words,0,&program)==NV2AVPR_SUCCESS && decodes==1);
    assert(program.steps==cache.steps);
    for(unsigned i=0;i<10000;i++) assert(pgraph_vsh_state_program(&cache,words,0,&program)==NV2AVPR_SUCCESS);
    assert(decodes==1); /* trailing invalid storage is never parsed */
    words[0][3]&=~1u;instruction(words[1],1,true);
    assert(pgraph_vsh_state_program(&cache,words,0,&program)==NV2AVPR_SUCCESS && decodes==3);
    words[0][3]|=1;
    assert(pgraph_vsh_state_program(&cache,words,0,&program)==NV2AVPR_SUCCESS && decodes==4);
    instruction(words[135],191,true);
    assert(pgraph_vsh_state_program(&cache,words,135,&program)==NV2AVPR_SUCCESS);
    words[135][3]&=~1u;
    assert(pgraph_vsh_state_program(&cache,words,135,&program)==NV2AVPR_BAD_PROGRAM);
    assert(pgraph_vsh_state_program(&cache,words,136,&program)==NV2AVPR_BAD_PROGRAM);
    assert(pgraph_vsh_state_program(&cache,words,UINT32_MAX,&program)==NV2AVPR_BAD_PROGRAM);
    memset(words,0,sizeof(words));words[0][1]=13u<<21;words[0][3]=(15u<<24)|1;
    assert(pgraph_vsh_state_program(&cache,words,0,&program)==NV2AVPR_ARL_CONFLICT && !cache.valid[0]);
    assert(pgraph_vsh_state_program(&cache,words,0,&program)==NV2AVPR_ARL_CONFLICT);
    instruction(words[0],0,true);parity(&cache,words,0,1);
    uint32_t saved[136][4];memcpy(saved,words,sizeof(saved));
    for(unsigned trial=0;trial<4000;trial++) {
        unsigned start=random_value()%136;
        unsigned length=1+random_value()%(136-start);
        for(unsigned i=0;i<length;i++) instruction(words[start+i],random_value()%192,i+1==length);
        parity(&cache,words,start,length);
        unsigned before=decodes;
        for(unsigned repeat=0;repeat<3;repeat++) parity(&cache,words,start,length);
        assert(decodes==before);
        /* Partial uploads, including ignored bits, must trigger exact-key
         * validation. Restore all source words without resetting the cache,
         * just as a savestate restore does. */
        for(unsigned word=0;word<4;word++) {
            unsigned index=start+random_value()%length;
            uint32_t old=words[index][word];
            words[index][word]^=word==0?1:word==1?1u<<8:word==2?1u<<10:1u<<12;
            parity(&cache,words,start,length);words[index][word]=old;
            parity(&cache,words,start,length);
        }
        memcpy(words,saved,sizeof(saved));parity(&cache,words,0,1);
    }
    static PGRAPHState handler;
    instruction(handler.program_data[0],0,true);
    Nv2aVshProgram reference;
    assert(nv2a_vsh_parse_program(&reference,handler.program_data[0],1)==NV2AVPR_SUCCESS);
    float constants[192*4]={0};bool dirty[192]={0};
    unsigned before=decodes;
    for(unsigned i=0;i<1000;i++) {
        float input[4];for(unsigned j=0;j<4;j++)input[j]=(random_value()%1001)/1000.f;
        memcpy(handler.vertex_state_shader_v0,input,sizeof(input));
        execute(&reference,constants,dirty,input);
        watch_allocations=true;launch_transform(&handler,0);watch_allocations=false;
        assert(!memcmp(handler.vsh_constants,constants,sizeof(constants)));
        assert(!memcmp(handler.vsh_constants_dirty,dirty,sizeof(dirty)));
    }
    assert(decodes==before+1 && !allocations);
    nv2a_vsh_program_destroy(&reference);
    printf("PASS: %u execution comparisons and 1000 production launches; exact constants/dirty masks; unchanged code decoded once; partial uploads, restored programs, overlapping starts, final-slot bounds and invalid programs; zero launch allocations\n",comparisons);
}

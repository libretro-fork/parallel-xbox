/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL stream line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
/* PRODUCTION */
static uint32_t rng=0x23c45a67;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static unsigned char memory[GL_INLINE_STREAM_BYTES];
/* The independent owner model tracks bytes consumed by each submitted GPU
 * frame. Failed optional reservations must neither overwrite nor advance. */
static uint64_t owners[GL_INLINE_STREAM_BYTES];
int main(void)
{
    GlInlineStreamCursor cursor={0};size_t offset=SIZE_MAX;
    assert(!gl_inline_stream_reserve(&cursor,0,0,&offset)&&offset==SIZE_MAX);
    assert(!gl_inline_stream_reserve(&cursor,SIZE_MAX,0,&offset));
    assert(gl_inline_stream_reserve(&cursor,1,0,&offset)&&offset==0);
    assert(gl_inline_stream_reserve(&cursor,1,0,&offset)&&offset==16);
    assert(gl_inline_stream_reserve(&cursor,GL_INLINE_STREAM_SLICE_BYTES-32,0,&offset)&&offset==32);
    assert(!gl_inline_stream_reserve(&cursor,1,0,&offset));
    gl_inline_stream_seal(&cursor,1);
    assert(gl_inline_stream_reserve(&cursor,17,0,&offset)&&offset==GL_INLINE_STREAM_SLICE_BYTES);
    gl_inline_stream_seal(&cursor,2);
    assert(gl_inline_stream_reserve(&cursor,19,0,&offset)&&offset==2*GL_INLINE_STREAM_SLICE_BYTES);
    gl_inline_stream_seal(&cursor,3);
    size_t before=cursor.used;
    assert(!gl_inline_stream_reserve(&cursor,1,0,&offset)&&cursor.used==before);
    assert(gl_inline_stream_reserve(&cursor,1,1,&offset)&&offset==0);
    gl_inline_stream_seal(&cursor,4);
    /* Even an empty slice must not make a stale serial valid after a reset. */
    gl_inline_stream_seal(&cursor,0);assert(cursor.disabled);
    assert(!gl_inline_stream_reserve(&cursor,1,UINT64_MAX,&offset));
    memset(&cursor,0,sizeof(cursor));
    uint64_t completed=0;unsigned accepted=0,rejected=0;
    for(uint64_t submitted=1;submitted<=20000;submitted++) {
        if(next()%3==0)completed=submitted-1;
        unsigned count=1+next()%80;
        for(unsigned draw=0;draw<count;draw++) {
            size_t size=1+next()%16384;
            if(next()%19==0)size=GL_INLINE_STREAM_SLICE_BYTES+1+next();
            GlInlineStreamCursor saved=cursor;
            offset=SIZE_MAX;
            bool success=gl_inline_stream_reserve(&cursor,size,completed,&offset);
            if(!success){assert(!memcmp(&cursor,&saved,sizeof(cursor))&&offset==SIZE_MAX);rejected++;continue;}
            assert(offset%16==0&&offset<=GL_INLINE_STREAM_BYTES&&size<=GL_INLINE_STREAM_BYTES-offset);
            for(size_t at=offset;at<offset+size;at++) {
                assert(owners[at]<=completed);
                owners[at]=submitted;
                memory[at]=(unsigned char)(submitted^at);
            }
            /* A subsequent unrelated reservation cannot alter this lend. */
            for(size_t at=offset;at<offset+size;at++)assert(memory[at]==(unsigned char)(submitted^at));
            accepted++;
        }
        gl_inline_stream_seal(&cursor,submitted);
        assert(!cursor.disabled);
    }
    assert(accepted>100000&&rejected>100000);
    cursor.serial[cursor.slice]=UINT64_MAX;
    gl_inline_stream_seal(&cursor,1);assert(cursor.disabled);
    printf("Mapped inline stream: 20000 frame schedules, %u nonoverlapping draws, %u pressure/oversize fallbacks, alignment and epoch reset PASS\n",accepted,rejected);
    return 0;
}

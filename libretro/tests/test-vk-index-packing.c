/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#define ROUND_UP(n,a) (((n)+(a)-1)&~((a)-1))
#define BUFFER_INDEX_STAGING 0
#define NV2A_PROF_GEOM_BUFFER_UPDATE_2 1
typedef uint64_t VkDeviceSize;
typedef struct { unsigned char *mapped; VkDeviceSize buffer_offset,buffer_size; } StorageBuffer;
typedef struct { StorageBuffer storage_buffers[1]; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; } PGRAPHState;
static size_t expected_bytes;static unsigned profiles,checks;
static void nv2a_profile_inc_counter(unsigned event) { assert(event==1);profiles++; }
static bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg,int index,VkDeviceSize bytes,unsigned align)
{
    StorageBuffer *b=&pg->vk_renderer_state->storage_buffers[0];
    assert(index==0 && align==1 && bytes==expected_bytes);checks++;
    return b->buffer_offset<=b->buffer_size && bytes<=b->buffer_size-b->buffer_offset;
}
/* PRODUCTION */
static uint64_t seed=17;
static uint32_t rnd(void) { seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed; }
int main(void)
{
    uint32_t storage[1024],expected[1024],indices[513],saved[513];
    PGRAPHVkState r;PGRAPHState pg={&r};
    unsigned cases=0;
    for(unsigned trial=0;trial<100000;trial++) {
        size_t count=trial%514,offset=(trial%33)*4;
        for(unsigned i=0;i<513;i++)indices[i]=trial%3 ? rnd()%65535 : 65534;
        memcpy(saved,indices,sizeof(saved));
        memset(storage,0xa5,sizeof(storage));memcpy(expected,storage,sizeof(storage));
        r.storage_buffers[0]=(StorageBuffer){(void *)storage,offset,sizeof(storage)};
        size_t bytes=((count+1)/2)*4;
        expected_bytes=bytes;profiles=checks=0;
        unsigned char *dst=(unsigned char *)expected+offset;
        for(size_t i=0;i<count;i++) {
            /* Independent wire oracle and Vulkan's u16 fetch interpretation. */
            dst[2*i]=indices[i]&255;dst[2*i+1]=indices[i]>>8;
        }
        if(count&1)dst[2*count]=dst[2*count+1]=0;
        VkDeviceSize actual=pgraph_vk_update_index_buffer_u16(&pg,indices,count);
        assert(actual==offset && profiles==1 && checks==1);
        assert(r.storage_buffers[0].buffer_offset==offset+bytes);
        assert(!memcmp(storage,expected,sizeof(storage)) && !memcmp(indices,saved,sizeof(saved)));
        const unsigned char *wire=(const unsigned char *)storage+offset;
        for(size_t i=0;i<count;i++)assert(((uint32_t)wire[2*i]|(uint32_t)wire[2*i+1]<<8)==indices[i]);
        /* Interleave the original u32 upload after both odd and even counts. */
        size_t next=r.storage_buffers[0].buffer_offset;assert(next%4==0);
        uint32_t wide=0x12345678;memcpy((char *)storage+next,&wide,4);
        memcpy((char *)expected+offset+bytes,&wide,4);
        assert(!memcmp(storage,expected,sizeof(storage)));cases++;
    }
    const uint32_t maxima[]={0,1,65533,65534,65535,65536,0xffffff,UINT32_MAX};
    for(unsigned primitive=0;primitive<256;primitive++)
    for(unsigned i=0;i<sizeof(maxima)/sizeof(maxima[0]);i++) {
        bool native=primitive<8 && ((0x76u>>primitive)&1);
        assert(inline_indices_use_u16(primitive,maxima[i])==(native && maxima[i]<65535));
        cases++;
    }
    printf("PASS: %u index upload/eligibility cases; exact fetch values, odd padding, u32 interleaving, guards, source ownership and restart boundary\n",cases);
    return 0;
}

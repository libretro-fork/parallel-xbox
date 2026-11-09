/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <retro_spsc.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
static retro_spsc_t vk_spec_in;
static retro_atomic_int_t vk_spec_cancel,vk_spec_inflight;
static int vk_spec_idle_ec;
static unsigned compiles,notifications,reads;
static size_t copied,start;
static bool active,borrow;
static ShaderState expected;
static unsigned char filler[32768];
static void retro_eventcount_notify(int *event) {assert(event==&vk_spec_idle_ec && !active);assert(retro_atomic_load_acquire_int(&vk_spec_inflight)==0);notifications++;}
static size_t observed_begin(retro_spsc_t *q,const void **ptr) {assert(!active);active=true;return retro_spsc_read_begin(q,ptr);}
static void observed_end(retro_spsc_t *q,size_t bytes) {
    assert(active);
    if(bytes) {
        assert(bytes==sizeof(ShaderState));
        assert(retro_atomic_load_acquire_int(&vk_spec_cancel)||compiles==1);
    }
    active=false;retro_spsc_read_end(q,bytes);
}
static size_t observed_read(retro_spsc_t *q,void *out,size_t bytes) {assert(!active);reads++;size_t n=retro_spsc_read(q,out,bytes);copied+=n;return n;}
static void vk_spec_compile_state(const ShaderState *state) {
    assert(!retro_atomic_load_acquire_int(&vk_spec_cancel));compiles++;
    assert(!memcmp(state,&expected,sizeof(expected)));
    assert(retro_atomic_load_acquire_int(&vk_spec_inflight)==1);
    size_t tail=retro_atomic_load_relaxed_size(&vk_spec_in.tail);
    if(borrow) {assert(active && tail==start && (const void *)state==vk_spec_in.buffer+(start&vk_spec_in.mask));}
    else {assert(!active && tail==start+sizeof(ShaderState));}
    /* The full producer cannot recycle a live lend. A copied fallback has
     * released its slot and must survive immediate producer reuse instead. */
    unsigned char poison=0xbd;
    assert(retro_spsc_write(&vk_spec_in,&poison,1)==(borrow?0u:1u));
    assert(!memcmp(state,&expected,sizeof(expected)));
}
#define retro_spsc_read_begin observed_begin
#define retro_spsc_read_end observed_end
#define retro_spsc_read observed_read
/* PRODUCTION */
#undef retro_spsc_read_begin
#undef retro_spsc_read_end
#undef retro_spsc_read
static void position(size_t offset) {
    retro_atomic_size_init(&vk_spec_in.head,offset);retro_atomic_size_init(&vk_spec_in.tail,offset);
    vk_spec_in.cached_head=vk_spec_in.cached_tail=offset;start=offset;
    compiles=notifications=reads=0;copied=0;active=false;
    retro_atomic_store_release_int(&vk_spec_inflight,1);
}
int main(void) {
    assert(retro_spsc_init(&vk_spec_in,4*sizeof(ShaderState)));
    /* The production input queue uses this ordinary byte-ring initializer. */
    unsigned char *storage=vk_spec_in.buffer;
    assert(!vk_spec_in.mirror && vk_spec_in.capacity==vk_spec_in.mask+1);
    assert(vk_spec_in.capacity<=sizeof(filler));
    for(size_t i=0;i<sizeof(expected);i++)((unsigned char *)&expected)[i]=(unsigned char)(i*37+11);
    unsigned cases=0,lends=0,fallbacks=0;
    for(size_t offset=0;offset<vk_spec_in.capacity;offset++)for(unsigned cancel=0;cancel<2;cancel++) {
        position(offset);retro_atomic_store_release_int(&vk_spec_cancel,cancel);
        assert(retro_spsc_write(&vk_spec_in,&expected,sizeof(expected))==sizeof(expected));
        size_t rest=vk_spec_in.capacity-sizeof(expected);assert(retro_spsc_write(&vk_spec_in,filler,rest)==rest);
        borrow=vk_spec_in.capacity-offset>=sizeof(expected) && (uintptr_t)(storage+offset)%_Alignof(ShaderState)==0;
        assert(vk_spec_process_input());
        assert(compiles==!cancel && notifications==1 && !active);
        assert(copied==(borrow?0:sizeof(expected)) && reads==!borrow);
        assert(retro_atomic_load_relaxed_size(&vk_spec_in.tail)==offset+sizeof(expected));
        if(borrow)lends++;else fallbacks++;cases++;
    }
    for(size_t bytes=0;bytes<sizeof(expected);bytes+=17) {
        position(vk_spec_in.capacity-9);retro_atomic_store_release_int(&vk_spec_cancel,0);
        assert(retro_spsc_write(&vk_spec_in,&expected,bytes)==bytes);
        assert(!vk_spec_process_input() && !compiles && !notifications && !reads && !copied && !active);
        assert(retro_atomic_load_relaxed_size(&vk_spec_in.tail)==start);cases++;
    }
    retro_spsc_free(&vk_spec_in);
    printf("Worker input: %u cases; %u lends, %u bounded wrap/alignment fallbacks; state_bytes=%zu; no contiguous handoff copy\n",cases,lends,fallbacks,sizeof(ShaderState));
}

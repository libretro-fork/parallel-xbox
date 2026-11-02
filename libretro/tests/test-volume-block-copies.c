/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) assert((x) == VK_SUCCESS)
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef struct { VkBuffer vram_buffer; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; uint8_t *arena; size_t head; } PGRAPHState;
typedef struct { uint8_t *vram_ptr; size_t *vram; PGRAPHState pgraph; } NV2AState;
#define memory_region_size(p) (*(p))
static VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *pg, size_t bytes,
                                           size_t align, uint8_t **out)
{
    size_t offset = (pg->head + align-1) & ~(align-1);
    pg->head = offset + bytes; *out = pg->arena + offset;
    return offset;
}
/* PRODUCTION */
/* Frozen fcea99f7cf staging control. */
static inline __attribute__((always_inline)) void old_stage_blocks_3d(PGRAPHState *pg, TextureLevel *level,
                            const uint8_t *data, unsigned int width,
                            unsigned int height, unsigned int depth,
                            size_t block_size)
{
    unsigned int bw = ((width + 3) & ~3) / 4, bh = ((height + 3) & ~3) / 4;
    size_t slice = (size_t)bw * bh * block_size;
    /* With one slice or one XY block, Xbox's four-deep block groups
     * already have Vulkan slice order. Keep those bytes in VRAM. */
    bool ordered = depth == 1 || bw * bh == 1;
    if (ordered &&
        level_from_vram(pg, level, data, width, height, depth,
                        slice * depth, block_size)) {
        return;
    }
    uint8_t *dst = stage_level(pg, level, width, height, depth, slice * depth);
    if (ordered) {
        memcpy(dst, data, slice * depth);
        return;
    }
    const uint8_t *src = data;
    for (unsigned int z0 = 0; z0 < depth; z0 += 4) {
        unsigned int nz = MIN(depth - z0, 4);
        for (unsigned int b = 0; b < bw * bh; b++) {
            for (unsigned int z = 0; z < nz; z++) {
                memcpy(dst + (z0 + z) * slice + b * block_size, src,
                       block_size);
                src += block_size;
            }
        }
    }
}

static void check(unsigned w,unsigned h,unsigned depth,unsigned bs,unsigned kind) {
    unsigned bw=(w+3)/4,bh=(h+3)/4;size_t bytes=(size_t)bw*bh*depth*bs;
    uint8_t *guest=malloc(bytes+128),*actual=malloc(bytes+64),*expected=malloc(bytes);
    assert(guest&&actual&&expected);unsigned offset=kind==2?36:32;
    memset(guest,0xAB,bytes+128);memset(actual,0xCD,bytes+64);
    for(unsigned z=0;z<depth;z++)for(unsigned block=0;block<bw*bh;block++)for(unsigned b=0;b<bs;b++) {
        unsigned group=(z/4)*4,nz=MIN(depth-group,4);
        size_t src=((size_t)group*bw*bh+(size_t)block*nz+z-group)*bs+b;
        uint8_t value=(uint8_t)(block*31+z*13+b*7);
        guest[offset+src]=value;expected[((size_t)z*bw*bh+block)*bs+b]=value;
    }
    size_t size=bytes+128;PGRAPHVkState renderer={.vram_buffer=kind?(VkBuffer)(uintptr_t)1:VK_NULL_HANDLE};
    NV2AState dev={.vram_ptr=guest,.vram=&size,.pgraph={&renderer,actual,3}};TextureLevel level;
    stage_blocks_3d(&dev.pgraph,&level,guest+offset,w,h,depth,bs);
    bool direct=kind&&offset%bs==0&&(depth==1||bw==1||((bh==1&&bw<=4)||(uint64_t)bw*bh*((depth+3)/4)<=64));
    assert(level.width==w&&level.height==h&&level.depth==depth&&level.size==bytes&&!level.row_length&&!level.crop_offset);
    assert(level.source==(direct?LEVEL_VRAM:LEVEL_STAGING));
    if(direct){
        assert(dev.pgraph.head==3&&level.offset==offset);
        for(unsigned z=0;z<depth;z++)for(unsigned block=0;block<bw*bh;block++){
            unsigned group=z&~3u,nz=MIN(depth-group,4);
            size_t src=((size_t)group*bw*bh+(size_t)block*nz+z-group)*bs;
            assert(!memcmp(guest+offset+src,expected+((size_t)z*bw*bh+block)*bs,bs));
        }
    }
    else{assert(level.offset==16&&dev.pgraph.head==16+bytes);assert(!memcmp(actual+16,expected,bytes));}
    for(unsigned i=0;i<16;i++)assert(actual[i]==0xCD);
    for(size_t i=16+bytes;i<bytes+64;i++)assert(actual[i]==0xCD);
    for(unsigned i=0;i<offset;i++)assert(guest[i]==0xAB);
    for(size_t i=offset+bytes;i<bytes+128;i++)assert(guest[i]==0xAB);
    free(guest);free(actual);free(expected);
}
static double now(void) {
#ifdef _WIN32
    LARGE_INTEGER f,v;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&v);return (double)v.QuadPart/f.QuadPart;
#else
    struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static __attribute__((noinline)) void run_old(PGRAPHState *pg,TextureLevel *level,
    const uint8_t *data,unsigned w,unsigned h,unsigned d,bool bc1) {
    old_stage_blocks_3d(pg,level,data,w,h,d,bc1?8:16);
}
static __attribute__((noinline)) void run_new(PGRAPHState *pg,TextureLevel *level,
    const uint8_t *data,unsigned w,unsigned h,unsigned d,bool bc1) {
    stage_blocks_3d(pg,level,data,w,h,d,bc1?8:16);
}
static void benchmark(void) {
    unsigned dims[][3]={{64,64,4},{256,256,4},{256,256,16},{512,256,32}};
    for(unsigned fmt=0;fmt<2;fmt++)for(unsigned k=0;k<4;k++) {
        unsigned w=dims[k][0],h=dims[k][1],d=dims[k][2],bs=fmt?16:8;
        size_t bytes=(size_t)((w+3)/4)*((h+3)/4)*d*bs,size=bytes;
        uint8_t *src=malloc(bytes),*out[2]={malloc(bytes),malloc(bytes)};assert(src&&out[0]&&out[1]);
        for(size_t i=0;i<bytes;i++)src[i]=(uint8_t)(i*37+(i>>8));
        PGRAPHVkState renderer={0};NV2AState dev={.vram_ptr=src,.vram=&size,.pgraph.vk_renderer_state=&renderer};
        unsigned iterations=16000000/bytes+50;double time[2]={0};
        for(unsigned round=0;round<8;round++)for(unsigned order=0;order<2;order++) {
            unsigned v=order^(round&1);dev.pgraph.arena=out[v];TextureLevel level;double start=now();
            for(unsigned n=0;n<iterations;n++) {dev.pgraph.head=0;
                if(v)run_new(&dev.pgraph,&level,src,w,h,d,bs==8);
                else run_old(&dev.pgraph,&level,src,w,h,d,bs==8);
            }time[v]+=now()-start;
        }
        assert(!memcmp(out[0],out[1],bytes));
        printf("%ux%ux%u BC%u bytes=%zu old=%.3f new=%.3f us/call (%u calls/round)\n",w,h,d,bs==8?1:3,bytes,time[0]*1e6/(8*iterations),time[1]*1e6/(8*iterations),iterations);
        free(src);free(out[0]);free(out[1]);
    }
}
int main(int argc,char **argv) {
    if(argc>1&&!strcmp(argv[1],"--bench")){benchmark();return 0;}
    unsigned widths[]={1,4,8,12,64,256},heights[]={1,4,8,20,64},depths[]={1,2,4,5,8,16},cases=0;
    for(unsigned f=0;f<3;f++)for(unsigned w=0;w<6;w++)for(unsigned h=0;h<5;h++)for(unsigned d=0;d<6;d++)for(unsigned kind=0;kind<3;kind++) {
        check(widths[w],heights[h],depths[d],f==0?8:f==1?16:24,kind);cases++;
    }
    puts("1620 coordinate-oracle cases: direct VRAM, reordered/fallback staging, partial Z groups, odd dimensions, guards and metadata PASS");
    assert(cases==1620);return 0;
}

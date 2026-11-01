/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "swizzle.h"
#define MAX(a,b) ((a)>(b)?(a):(b))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef struct { unsigned color_format; } TextureShape;
typedef struct { uint8_t *tex_scratch[2]; size_t tex_scratch_size[2]; } PGRAPHGLState;
typedef struct { uint8_t *arena; size_t head,capacity; unsigned allocations; } PGRAPHState;
typedef struct { uint8_t *vram_ptr; PGRAPHState pgraph; } NV2AState;
typedef uint64_t VkDeviceSize;
typedef void *VkCommandBuffer;
typedef struct {
    unsigned width,height,depth,row_length,crop_offset;
    size_t size; VkDeviceSize offset;
    enum { LEVEL_STAGING, LEVEL_UNSWIZZLED } source;
} TextureLevel;
static unsigned cpu_conversions, raw_unswizzles, allocations, gpu_calls;
static bool gpu_available;
static void *checked_malloc(size_t n) {
    allocations++; void *p=malloc(n); assert(p); return p;
}
#define g_malloc checked_malloc
#define g_free free
static VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *p,size_t n,size_t align,uint8_t **out) {
    size_t off=(p->head+align-1)&~(align-1);
    assert(off+n<=p->capacity); p->head=off+n; p->allocations++;
    *out=p->arena+off; return off;
}
static bool pgraph_vk_unswizzle(PGRAPHState *p,VkCommandBuffer c,uintptr_t addr,
    unsigned w,unsigned h,unsigned d,unsigned bpp,uintptr_t palette,VkDeviceSize *out) {
    (void)p;(void)c;(void)addr;(void)w;(void)h;(void)d;
    assert(bpp==1 && palette);gpu_calls++;*out=77;return gpu_available;
}
static void pgraph_yuv422_to_rgba(uint8_t *out,const uint8_t *in,unsigned w,bool uyvy) {
    (void)out;(void)in;(void)w;(void)uyvy;abort();
}
/* Instrument only the old linear intermediate paths in extracted callers. */
static void counted_unswizzle(const uint8_t *src,unsigned w,unsigned h,unsigned d,
    uint8_t *dst,unsigned row,unsigned slice,unsigned bpp) {
    raw_unswizzles++;unswizzle_box(src,w,h,d,dst,row,slice,bpp);
}
#define unswizzle_box counted_unswizzle
#define unswizzle_rect(s,w,h,d,p,b) counted_unswizzle(s,w,h,1,d,p,0,b)
/* These conversion fixtures never take the unconverted direct-VRAM path. */
#define level_from_vram(...) false
/* PRODUCTION */
#undef unswizzle_box
#undef unswizzle_rect

static size_t morton(unsigned x,unsigned y,unsigned z,unsigned w,unsigned h,unsigned d) {
    size_t out=0; unsigned lane=0;
    for(unsigned bit=1;bit<w||bit<h||bit<d;bit<<=1) {
        if(bit<w){if(x&bit)out|=(size_t)1<<lane;lane++;}
        if(bit<h){if(y&bit)out|=(size_t)1<<lane;lane++;}
        if(bit<d){if(z&bit)out|=(size_t)1<<lane;lane++;}
    } return out;
}
static void run_case(unsigned w,unsigned h,unsigned d,unsigned shift) {
    size_t n=(size_t)w*h*d, bytes=n*4;
    uint8_t *guest=malloc(n+2048),*expected=malloc(bytes),*output=malloc(bytes+32);
    uint8_t *arena=malloc(bytes+64),*copy=malloc(n+2048);
    assert(guest&&expected&&output&&arena&&copy);
    uint8_t *src=guest+17+shift,*palette=guest+n+64+shift;
    memset(guest,0xA3,n+2048);
    for(size_t i=0;i<n;i++)src[i]=(uint8_t)(i*37+(i>>8)*19+shift);
    for(unsigned i=0;i<1024;i++)palette[i]=(uint8_t)(i*13+(i>>2)*17+shift);
    memcpy(copy,guest,n+2048);
    for(unsigned z=0;z<d;z++)for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
        memcpy(expected+(((size_t)z*h+y)*w+x)*4,
            palette+(size_t)src[morton(x,y,z,w,h,d)]*4,4);
    memset(output,0xAB,bytes+32);
    unswizzle_box_palette(src,palette,w,h,d,output+shift);
    assert(!memcmp(output+shift,expected,bytes));
    for(unsigned i=0;i<shift;i++)assert(output[i]==0xAB);
    for(size_t i=bytes+shift;i<bytes+32;i++)assert(output[i]==0xAB);
    TextureShape shape={NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8};
    PGRAPHGLState gl={0}; raw_unswizzles=cpu_conversions=allocations=0;
    const uint8_t *result=unswizzle_level(&gl,&shape,src,palette,w,h,d,1);
    assert(!memcmp(result,expected,bytes));
    assert(!gl.tex_scratch[0]&&gl.tex_scratch_size[0]==0&&allocations==1&&!raw_unswizzles);
    assert(unswizzle_level(&gl,&shape,src,palette,w,h,d,1)==result&&allocations==1);
    NV2AState dev={.vram_ptr=guest,.pgraph={.arena=arena,.head=3,.capacity=bytes+64}};
    TextureLevel level={0};gpu_available=false;gpu_calls=0;
    memset(arena,0xAB,bytes+64);
    stage_swizzled(&dev.pgraph,NULL,&level,&shape,src,palette,w,h,d,1);
    assert(gpu_calls==1&&dev.pgraph.allocations==1&&dev.pgraph.head==16+bytes);
    assert(level.source==LEVEL_STAGING&&level.offset==16&&level.size==bytes);
    assert(level.width==w&&level.height==h&&level.depth==d&&!level.row_length&&!level.crop_offset);
    assert(!memcmp(arena+16,expected,bytes)&&!raw_unswizzles);
    for(unsigned i=0;i<16;i++)assert(arena[i]==0xAB);
    for(size_t i=16+bytes;i<bytes+64;i++)assert(arena[i]==0xAB);
    /* GPU success still takes the original early return, with no CPU allocation. */
    dev.pgraph.head=3;dev.pgraph.allocations=0;gpu_available=true;
    stage_swizzled(&dev.pgraph,NULL,&level,&shape,src,palette,w,h,d,1);
    assert(gpu_calls==2&&!dev.pgraph.allocations&&dev.pgraph.head==3);
    assert(level.source==LEVEL_UNSWIZZLED&&level.offset==77&&level.size==bytes);
    assert(!memcmp(copy,guest,n+2048));
    free(gl.tex_scratch[1]);free(guest);free(copy);free(expected);free(output);free(arena);
}
static double now(void) {
#ifdef _WIN32
    LARGE_INTEGER v,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&v);
    return (double)v.QuadPart/f.QuadPart;
#else
    struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static void benchmark(void) {
    unsigned dims[][3]={{32,32,1},{256,256,1},{1024,1024,1},{64,64,16}};
    for(unsigned k=0;k<4;k++) {
        unsigned w=dims[k][0],h=dims[k][1],d=dims[k][2];size_t n=(size_t)w*h*d;
        uint8_t *src=malloc(n),*tmp=malloc(n),*old=malloc(n*4),*fused=malloc(n*4);
        uint32_t pal[256];for(unsigned i=0;i<256;i++)pal[i]=i*0x01020408u;
        for(size_t i=0;i<n;i++)src[i]=(uint8_t)(i*37+(i>>8)*19);
        TextureShape shape={NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8};
        unsigned iters=(unsigned)(16000000/n)+20;
        double times[2]={0};
        for(unsigned round=0;round<8;round++)for(unsigned pass=0;pass<2;pass++) {
            unsigned variant=pass^(round&1);double start=now();
            for(unsigned i=0;i<iters;i++) {
                if(variant)unswizzle_box_palette(src,(uint8_t*)pal,w,h,d,fused);
                else {unswizzle_box(src,w,h,d,tmp,w,w*h,1);
                    pgraph_convert_texture_data_into(shape,tmp,(uint8_t*)pal,w,h,d,w,w*h,old);}
            }
            times[variant]+=now()-start;
        }
        assert(!memcmp(old,fused,n*4));
        printf("%ux%ux%u old=%.3f fused=%.3f us/call (alternating 8 rounds, %u calls/round)\n",w,h,d,times[0]*1e6/(8*iters),times[1]*1e6/(8*iters),iters);
        free(src);free(tmp);free(old);free(fused);
    }
}
int main(int argc,char **argv) {
    if(argc>1&&strcmp(argv[1],"--bench")==0){benchmark();return 0;}
    unsigned widths[]={1,2,4,8,32,64,256},depths[]={1,2,4,8},cases=0;
    size_t texels=0;
    for(unsigned a=0;a<7;a++)for(unsigned b=0;b<7;b++)for(unsigned c=0;c<4;c++)for(unsigned shift=0;shift<8;shift++) {
        run_case(widths[a],widths[b],depths[c],shift);cases++;texels+=(size_t)widths[a]*widths[b]*depths[c];
    }
    printf("%u cases / %zu texels: palette Morton oracle, unaligned/guards, GL one scratch, Vulkan one final range, GPU early return and source immutability PASS\n",cases,texels);
    return 0;
}

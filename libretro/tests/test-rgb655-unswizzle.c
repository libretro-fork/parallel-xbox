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
static unsigned raw_unswizzles, allocations, gpu_calls;
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

static size_t morton(unsigned x,unsigned y,unsigned w,unsigned h) {
    size_t result=0;unsigned lane=0;
    for(unsigned bit=1;bit<w||bit<h;bit<<=1) {
        if(bit<w){if(x&bit)result|=(size_t)1<<lane;lane++;}
        if(bit<h){if(y&bit)result|=(size_t)1<<lane;lane++;}
    }return result;
}
static void reference(uint16_t word,uint8_t *out) {
    out[0]=((word>>10)&63)*127/63;
    out[1]=(uint8_t)((int)((((word>>5)&31)^16)*255/31)-128);
    out[2]=(uint8_t)((int)(((word&31)^16)*255/31)-128);
    out[3]=127;
}
static void run_case(unsigned w,unsigned h,unsigned shift) {
    size_t n=(size_t)w*h;
    uint8_t *source=malloc(n*2+32),*rgb=malloc(n*3+32),*rgba=malloc(n*4+32);
    uint8_t *old=malloc(n*3),*linear=malloc(n*2),*arena=malloc(n*4+64);
    uint8_t *expected=malloc(n*4),*copy=malloc(n*2+32);
    assert(source&&rgb&&rgba&&old&&linear&&arena&&expected&&copy);
    memset(source,0xAB,n*2+32);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        size_t i=(size_t)y*w+x;
        uint16_t word=(uint16_t)(i*40503+shift*17);
        memcpy(source+shift+morton(x,y,w,h)*2,&word,2);
        memcpy(linear+i*2,&word,2);reference(word,expected+i*4);
    }
    memcpy(copy,source,n*2+32);memset(rgb,0xAB,n*3+32);memset(rgba,0xAB,n*4+32);
    pgraph_convert_swizzled_rgb655_into(source+shift,w,h,false,rgb+shift);
    pgraph_convert_swizzled_rgb655_into(source+shift,w,h,true,rgba+shift);
    TextureShape shape={NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5};
    pgraph_convert_texture_data_into(shape,linear,NULL,w,h,1,w*2,n*2,old);
    for(size_t i=0;i<n;i++) {
        assert(!memcmp(rgb+shift+i*3,expected+i*4,3));
        assert(!memcmp(old+i*3,expected+i*4,3));
    }
    assert(!memcmp(rgba+shift,expected,n*4));
    for(unsigned i=0;i<shift;i++)assert(rgb[i]==0xAB&&rgba[i]==0xAB);
    for(size_t i=shift+n*3;i<n*3+32;i++)assert(rgb[i]==0xAB);
    for(size_t i=shift+n*4;i<n*4+32;i++)assert(rgba[i]==0xAB);
    PGRAPHGLState gl={0};raw_unswizzles=allocations=gpu_calls=0;
    const uint8_t *result=unswizzle_level(&gl,&shape,source+shift,NULL,w,h,1,2);
    assert(!memcmp(result,rgb+shift,n*3)&&!gl.tex_scratch[0]&&allocations==1);
    assert(unswizzle_level(&gl,&shape,source+shift,NULL,w,h,1,2)==result&&allocations==1);
    NV2AState dev={.vram_ptr=source,.pgraph={.arena=arena,.head=3,.capacity=n*4+64}};
    TextureLevel level={0};memset(arena,0xAB,n*4+64);
    stage_swizzled(&dev.pgraph,NULL,&level,&shape,source+shift,NULL,w,h,1,2);
    assert(!raw_unswizzles&&!gpu_calls&&dev.pgraph.allocations==1);
    assert(level.offset==16&&level.size==n*4&&dev.pgraph.head==16+n*4);
    assert(level.width==w&&level.height==h&&level.depth==1&&!level.crop_offset&&!level.row_length);
    assert(level.source==LEVEL_STAGING&&!memcmp(arena+16,expected,n*4));
    for(unsigned i=0;i<16;i++)assert(arena[i]==0xAB);
    for(size_t i=16+n*4;i<n*4+64;i++)assert(arena[i]==0xAB);
    assert(!memcmp(source,copy,n*2+32));
    free(gl.tex_scratch[1]);free(source);free(rgb);free(rgba);free(old);
    free(linear);free(arena);free(expected);free(copy);
}
static double now(void) {
#ifdef _WIN32
    LARGE_INTEGER v,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&v);
    return (double)v.QuadPart/f.QuadPart;
#else
    struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static void old_expand(uint8_t *pixels,size_t count) {
    while(count){size_t i=--count;pixels[4*i+2]=pixels[3*i+2];pixels[4*i+1]=pixels[3*i+1];pixels[4*i]=pixels[3*i];pixels[4*i+3]=127;}
}
/* Frozen c07b7f1065 converter, same source and flags as the baseline. */
static void old_convert(const TextureShape s,
                                      const uint8_t *data,
                                      const uint8_t *palette_data,
                                      unsigned int width, unsigned int height,
                                      unsigned int depth,
                                      unsigned int row_pitch,
                                      unsigned int slice_pitch,
                                      uint8_t *converted_data)
{
    if (s.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8) {
        const uint8_t *src = data;
        uint32_t *dst = (uint32_t *)converted_data;
        for (int z = 0; z < depth; z++) {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    uint8_t index = src[y * row_pitch + x];
                    uint32_t color = *(uint32_t *)(palette_data + index * 4);
                    *dst++ = color;
                }
            }
            src += slice_pitch;
        }
    } else if (s.color_format ==
                   NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8 ||
               s.color_format ==
                   NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8) {
        // TODO: Investigate whether a non-1 depth is possible.
        // Generally the hardware asserts when attempting to use volumetric
        // textures in linear formats.
        assert(depth == 1); /* FIXME */
        // FIXME: only valid if control0 register allows for colorspace
        // conversion
        bool uyvy = s.color_format ==
                    NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8;
        for (int y = 0; y < height; y++) {
            pgraph_yuv422_to_rgba(converted_data + (size_t)y * width * 4,
                                 data + (size_t)y * row_pitch * depth,
                                 width, uyvy);
        }
    } else if (s.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5) {
        assert(depth == 1); /* FIXME */
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t rgb655 = *(uint16_t *)(data + y * row_pitch + x * 2);
                int8_t *pixel = (int8_t *)&converted_data[(y * width + x) * 3];
                /* Maps 5 bit G and B signed value range to 8 bit
                 * signed values. R is probably unsigned.
                 */
                rgb655 ^= (1 << 9) | (1 << 4);
                pixel[0] = ((rgb655 & 0xFC00) >> 10) * 0x7F / 0x3F;
                pixel[1] = ((rgb655 & 0x03E0) >> 5) * 0xFF / 0x1F - 0x80;
                pixel[2] = (rgb655 & 0x001F) * 0xFF / 0x1F - 0x80;
            }
        }
    } else {
        assert(!"no conversion for this format");
    }
}
static void benchmark(void) {
    unsigned sizes[]={32,256,1024};TextureShape shape={NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5};
    for(unsigned size=0;size<3;size++)for(unsigned rgba=0;rgba<2;rgba++) {
        unsigned w=sizes[size];size_t n=(size_t)w*w;
        uint16_t *src=malloc(n*2),*tmp=malloc(n*2);uint8_t *old=malloc(n*4),*fused=malloc(n*4);
        assert(src&&tmp&&old&&fused);for(size_t i=0;i<n;i++)src[i]=(uint16_t)(i*40503);
        unsigned iters=(unsigned)(8000000/n)+20;double times[2]={0};
        for(unsigned round=0;round<8;round++)for(unsigned pass=0;pass<2;pass++) {
            unsigned variant=pass^(round&1);double start=now();
            for(unsigned i=0;i<iters;i++) {
                if(variant)pgraph_convert_swizzled_rgb655_into((uint8_t*)src,w,w,rgba,fused);
                else {unswizzle_box((uint8_t*)src,w,w,1,(uint8_t*)tmp,w*2,n*2,2);
                    old_convert(shape,(uint8_t*)tmp,NULL,w,w,1,w*2,n*2,old);
                    if(rgba)old_expand(old,n);}
            }times[variant]+=now()-start;
        }
        assert(!memcmp(old,fused,n*(rgba?4:3)));
        printf("%ux%u %s old=%.3f fused=%.3f us/call (8 alternating rounds, %u calls/round)\n",w,w,rgba?"RGBA":"RGB",times[0]*1e6/(8*iters),times[1]*1e6/(8*iters),iters);
        free(src);free(tmp);free(old);free(fused);
    }
}
int main(int argc,char **argv) {
    if(argc>1&&!strcmp(argv[1],"--bench")){benchmark();return 0;}
    unsigned sizes[]={1,2,4,8,32,64,256},cases=0;size_t texels=0;
    for(unsigned a=0;a<7;a++)for(unsigned b=0;b<7;b++)for(unsigned shift=0;shift<8;shift++) {
        run_case(sizes[a],sizes[b],shift);cases++;texels+=(size_t)sizes[a]*sizes[b];
    }
    puts("All 65536 RGB655 words exhaustively covered in each 256x256 case.");
    printf("%u cases / %zu texels: RGB/RGBA SNORM mapping, unaligned/guards, one GL scratch, one final Vulkan range, source immutability PASS\n",cases,texels);
    return 0;
}

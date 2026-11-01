/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <features/features_cpu.h>
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define GLO_HAVE_AVX2_SWAP
#define GLO_HAVE_SSE2_SWAP
#endif
static bool portable,sse2;
uint64_t cpu_features_get(void){
#ifdef GLO_HAVE_AVX2_SWAP
 return portable?0:(!sse2&&__builtin_cpu_supports("avx2")?RETRO_SIMD_AVX2:__builtin_cpu_supports("sse2")?RETRO_SIMD_SSE2:0);
#else
 return 0;
#endif
}
typedef unsigned GLenum;typedef uint8_t GLubyte;
#define GL_PACK_ROW_LENGTH 1
#define GL_PACK_ALIGNMENT 2
static int row_length=37,alignment=8;static unsigned bpp,reads;static bool pbo;
static void glGetIntegerv(GLenum name,int *out){*out=name==GL_PACK_ROW_LENGTH?row_length:alignment;}
static void glPixelStorei(GLenum name,int value){if(name==GL_PACK_ROW_LENGTH)row_length=value;else alignment=value;}
static uint8_t pixel(unsigned y,size_t x){return (uint8_t)(y*37+x*13+(x>>5)*7);}
static void glReadPixels(unsigned x,unsigned y,unsigned w,unsigned h,GLenum format,GLenum type,void *data){
 (void)format;(void)type;assert(!x&&!y&&alignment==1);reads++;if(pbo||!w||!h)return;
 for(unsigned r=0;r<h;r++)for(size_t c=0;c<(size_t)w*bpp;c++)((uint8_t*)data)[(size_t)r*row_length*bpp+c]=pixel(r,c);
}
/* PRODUCTION */
static __attribute__((noinline)) void old_flip(uint8_t *data,unsigned stride,size_t bytes,unsigned h){
 uint8_t *tmp=malloc(bytes);assert(tmp);for(unsigned y=0;y<h/2;y++){uint8_t *a=data+(size_t)y*stride,*b=data+(size_t)(h-1-y)*stride;memcpy(tmp,a,bytes);memcpy(a,b,bytes);memcpy(b,tmp,bytes);}free(tmp);
}
static void correctness(void){unsigned widths[]={0,1,2,3,7,8,9,15,16,17,63,64,65,640,1920},heights[]={0,1,2,3,7,16},formats[]={1,2,3,4,8},pads[]={0,1,7,64},offsets[]={0,1,3,7};unsigned cases=0;
 for(unsigned wi=0;wi<15;wi++)for(unsigned hi=0;hi<6;hi++)for(unsigned fi=0;fi<5;fi++)for(unsigned pi=0;pi<4;pi++)for(unsigned oi=0;oi<4;oi++)for(unsigned flip=0;flip<2;flip++){
  unsigned w=widths[wi],h=heights[hi],off=16+offsets[oi];bpp=formats[fi];unsigned stride=(w+pads[pi])*bpp;size_t length=(size_t)stride*h;
  uint8_t *data=malloc(length+64),*expected=malloc(length+64);assert(data&&expected);memset(data,0xA5,length+64);memset(expected,0xA5,length+64);
  for(unsigned y=0;y<h;y++)for(size_t x=0;x<(size_t)w*bpp;x++)expected[off+(size_t)y*stride+x]=pixel(flip?h-1-y:y,x);
  row_length=37;alignment=8;unsigned previous=reads;
  glo_readpixels(0,0,bpp,stride,w,h,flip,data+off);
  assert(reads==previous+1&&row_length==37&&alignment==8);assert(!memcmp(data,expected,length+64));
  free(data);free(expected);cases++;
 }
 bpp=4;glo_readpixels(0,0,bpp,0,0,0,true,NULL);assert(row_length==37&&alignment==8);
 pbo=true;glo_readpixels(0,0,bpp,256,64,64,false,NULL);assert(row_length==37&&alignment==8);pbo=false;
 printf("%u client readback cases: byte-exact flip/no-flip, unaligned/tail/odd rows, padding/guards, zero dimensions, pack state and non-flipped PBO handoff PASS\n",cases);
}
static double monotonic_seconds(void){
#ifdef _WIN32
 LARGE_INTEGER count,freq;assert(QueryPerformanceCounter(&count)&&QueryPerformanceFrequency(&freq));return (double)count.QuadPart/freq.QuadPart;
#else
 struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;
#endif
}
static void benchmark(void){unsigned widths[]={64,640,1920,6400},heights[]={64,480,1080,64};volatile unsigned checksum=0;
 for(unsigned t=0;t<4;t++){
  unsigned width=widths[t],height=heights[t],stride=width*4;size_t bytes=(size_t)stride*height;
  uint8_t *image=malloc(bytes);assert(image);memset(image,0x3C,bytes);unsigned n=bytes<1024*1024?1000:100;
  double totals[2]={0};
  for(unsigned round=0;round<8;round++)for(unsigned order=0;order<2;order++) {
   unsigned variant=order^(round&1);double a=monotonic_seconds();
   for(unsigned i=0;i<n;i++){if(variant)glo_flip_rows(image,stride,stride,height);else old_flip(image,stride,stride,height);checksum+=image[i%bytes];}
   totals[variant]+=monotonic_seconds()-a;
  }
  printf("%ux%u isolated flip: old %.3f us, in-place %.3f us per image\n",width,height,1e6*totals[0]/n/8,1e6*totals[1]/n/8);free(image);
 }assert(checksum);
}
int main(int argc,char **argv){portable=getenv("GL_FLIP_PORTABLE")!=NULL||(argc>1&&!strcmp(argv[1],"portable"))||(argc>2&&!strcmp(argv[2],"portable"));sse2=(argc>1&&!strcmp(argv[1],"sse2"))||(argc>2&&!strcmp(argv[2],"sse2"));printf("swap route=%s\n",cpu_features_get()&RETRO_SIMD_AVX2?"AVX2":cpu_features_get()&RETRO_SIMD_SSE2?"SSE2":"portable");if(argc>1&&!strcmp(argv[1],"bench"))benchmark();else correctness();return 0;}

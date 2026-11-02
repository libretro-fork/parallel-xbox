
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libretro.h>
#include <encodings/xxh3.h>
static uint64_t mask;
uint64_t cpu_features_get(void) { return mask; }
static uint64_t rng=0x39ab12cd98765432ULL;
static uint64_t next(void){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
int main(int argc,char **argv) {
 assert(argc==2);
 unsigned mode=(unsigned)atoi(argv[1]);
#if defined(__x86_64__) || defined(__i386__)
 __builtin_cpu_init();
 if(mode==1) {if(!__builtin_cpu_supports("sse2"))return 77;mask=RETRO_SIMD_SSE2;}
 if(mode==2) {if(!__builtin_cpu_supports("avx2"))return 77;mask=RETRO_SIMD_SSE2|RETRO_SIMD_AVX2;}
 if(mode==3) {if(!__builtin_cpu_supports("avx512f")||!__builtin_cpu_supports("avx512bw"))return 77;mask=RETRO_SIMD_SSE2|RETRO_SIMD_AVX2|RETRO_SIMD_AVX512;}
#else
 if(mode)return 77;
#endif
 static uint8_t a[32784],b[32784],joined[65536];
 for(size_t i=0;i<sizeof(a);i++){a[i]=(uint8_t)next();b[i]=(uint8_t)next();}
 const size_t sizes[]={0,1,15,16,17,63,64,65,127,128,129,191,192,239,240,241,255,256,257,1023,1024,1025,2048,2304,2356,2620,4096,16384,32768};
 unsigned cases=0;
 for(unsigned trial=0;trial<250000;trial++) {
   size_t na,nb;
   if(trial<sizeof(sizes)/sizeof(*sizes)*sizeof(sizes)/sizeof(*sizes)) {
     na=sizes[trial%(sizeof(sizes)/sizeof(*sizes))];nb=sizes[trial/(sizeof(sizes)/sizeof(*sizes))];
   } else { na=(size_t)(next()%32769);nb=(size_t)(next()%32769);if(trial&1)na&=~(size_t)63; }
   size_t oa=next()%16,ob=next()%16;
   uint64_t seed=(trial%4)?next():0;
   memcpy(joined,a+oa,na);memcpy(joined+na,b+ob,nb);
   uint64_t expected=xxh3_64(joined,na+nb,seed);
   uint64_t actual=xxh3_64_pair(na?a+oa:NULL,na,nb?b+ob:NULL,nb,seed);
   if(actual!=expected){fprintf(stderr,"mismatch mode=%u first=%zu second=%zu seed=%llu\n",mode,na,nb,(unsigned long long)seed);return 1;}
   cases++;
 }
 printf("backend=%u: %u seeded, boundary, unaligned and empty-span cases passed\n",mode,cases);
 return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif
#undef assert
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
/* PRODUCTION */
static __attribute__((noinline)) void reference(uint8_t *out,const uint8_t *in,unsigned width,bool uyvy){
 for(unsigned x=0;x<width;x++,out+=4){if(uyvy)convert_uyvy_to_rgb(in,x,out,out+1,out+2);else convert_yuy2_to_rgb(in,x,out,out+1,out+2);out[3]=255;}
}
static __attribute__((noinline)) void candidate(uint8_t *out,const uint8_t *in,unsigned width,bool uyvy){pgraph_yuv422_to_rgba(out,in,width,uyvy);}
static uint32_t rng=0x5328ed91;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static size_t bytes(unsigned width,bool uyvy){return !width?0:(width%2?width*2+(uyvy?1:2):width*2);}
static uint8_t *guarded(size_t *page){
#ifdef _WIN32
 SYSTEM_INFO info;GetSystemInfo(&info);*page=info.dwPageSize;
 uint8_t *p=VirtualAlloc(NULL,*page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(p);DWORD old;assert(VirtualProtect(p+*page,*page,PAGE_NOACCESS,&old));return p;
#else
 *page=(size_t)sysconf(_SC_PAGESIZE);uint8_t *p=mmap(NULL,*page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);assert(!mprotect(p+*page,*page,PROT_NONE));return p;
#endif
}
int main(int argc,char **argv){
 uint8_t in[4096+64],out[8192+64],want[8192+64];unsigned long long cases=0;
 for(unsigned uyvy=0;uyvy<2;uyvy++)for(unsigned y=0;y<256;y++)for(unsigned u=0;u<256;u++)for(unsigned v=0;v<256;v++){
  uint8_t pair[4];pair[uyvy?1:0]=y;pair[uyvy?3:2]=255-y;pair[uyvy?0:1]=u;pair[uyvy?2:3]=v;
  reference(want,pair,2,uyvy);candidate(out,pair,2,uyvy);assert(!memcmp(out,want,8));cases++;
 }
 for(unsigned n=0;n<20000;n++){
  unsigned width=random32()%1025,uyvy=n&1,si=random32()%32,di=random32()%32;
  for(unsigned j=0;j<sizeof(in);j++){in[j]=(uint8_t)random32();}
  memset(out,0xa5,sizeof(out));memset(want,0xa5,sizeof(want));
  reference(want+di,in+si,width,uyvy);candidate(out+di,in+si,width,uyvy);assert(!memcmp(out,want,sizeof(out)));cases++;
 }
 size_t page,other;uint8_t *src=guarded(&page),*dst=guarded(&other);assert(page==other);
 for(unsigned uyvy=0;uyvy<2;uyvy++)for(unsigned width=0;width<=1024;width++){
  size_t used=bytes(width,uyvy);for(size_t j=0;j<used;j++)src[page-used+j]=(uint8_t)random32();
  reference(want,src+page-used,width,uyvy);candidate(dst+page-width*4,src+page-used,width,uyvy);assert(!memcmp(want,dst+page-width*4,width*4));cases++;
 }
#ifdef _WIN32
 assert(VirtualFree(src,0,MEM_RELEASE)&&VirtualFree(dst,0,MEM_RELEASE));
#else
 assert(!munmap(src,page*2)&&!munmap(dst,page*2));
#endif
 pgraph_yuv422_to_rgba(NULL,NULL,0,false);
 printf("YUV422: %llu exhaustive/chroma-pair/alignment/odd-width/guard-page cases, byte-exact RGBA and no output overrun passed\n",cases);
 if(argc>1 && !strcmp(argv[1],"--bench")){
  unsigned widths[]={640,720,1920},heights[]={480,576,1080};volatile unsigned checksum=0;
  for(unsigned size=0;size<3;size++){
   unsigned w=widths[size],h=heights[size],repeats=40;size_t count=(size_t)w*h;
   uint8_t *s=malloc(count*2),*d=malloc(count*4);assert(s&&d);for(size_t i=0;i<count*2;i++)s[i]=(uint8_t)random32();
   for(unsigned fmt=0;fmt<2;fmt++){
    double times[2]={0};
    for(unsigned pass=0;pass<4;pass++)for(unsigned slot=0;slot<2;slot++){
     unsigned kind=slot^(pass&1);clock_t start=clock();
     for(unsigned n=0;n<repeats;n++){for(unsigned y=0;y<h;y++){if(kind)candidate(d+(size_t)y*w*4,s+(size_t)y*w*2,w,fmt);else reference(d+(size_t)y*w*4,s+(size_t)y*w*2,w,fmt);}checksum+=d[n%count*4];}
     times[kind]+=(double)(clock()-start)/CLOCKS_PER_SEC;
    }
    printf("YUV422 %ux%u %s: scalar %.3f ms/frame paired %.3f ms/frame\n",w,h,fmt?"UYVY":"YUY2",times[0]*1000/(repeats*4),times[1]*1000/(repeats*4));
   }
   free(s);free(d);
  }
  printf("benchmark checksum %u\n",checksum);
 }
 return 0;
}

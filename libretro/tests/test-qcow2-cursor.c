/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "block/libretro-qcow2.h"
/* PRODUCTION */
/* Independent scalar oracle: one input descriptor at a time, no coalescing. */
static void oracle(Cursor*c,uint8_t*buffer,size_t len,int mode){
 while(len){assert(c->i<c->n);size_t n=c->iov[c->i].len-c->off;if(n>len)n=len;
  if(n){uint8_t*p=(uint8_t*)c->iov[c->i].base+c->off;if(mode==0)memcpy(p,buffer,n);else if(mode==1)memcpy(buffer,p,n);else memset(p,0,n);buffer+=n;len-=n;c->off+=n;}
  if(c->off==c->iov[c->i].len){c->i++;c->off=0;}
 }
}
static void __attribute__((noinline)) apply_out(Cursor*c,uint8_t*b,size_t len){cursor_copy_out(c,b,len);}
static void __attribute__((noinline)) apply_in(Cursor*c,uint8_t*b,size_t len){cursor_copy_in(c,b,len);}
static void __attribute__((noinline)) apply_zero(Cursor*c,uint8_t*b,size_t len){(void)b;cursor_zero(c,len);}
static void apply(Cursor*c,uint8_t*b,size_t len,int mode){if(mode==0)apply_out(c,b,len);else if(mode==1)apply_in(c,b,len);else apply_zero(c,b,len);}
static uint32_t seed=0x521af34b;
static unsigned random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint8_t actual[131072],expected[131072],buffer[65536],reference[65536];
static void check(void){
 for(unsigned trial=0;trial<10000;trial++){
  LRFileIOV a[64],b[64];int n=1+(int)(random_word()%64);size_t offset=0,total=0;
  for(int i=0;i<n;i++){size_t len=random_word()%513;if((trial+i)%7==0)len=0;size_t gap=random_word()%3==0?16:0;offset+=gap;
   a[i]=(LRFileIOV){len?actual+offset:NULL,len};b[i]=(LRFileIOV){len?expected+offset:NULL,len};offset+=len;total+=len;}
  size_t start=total?random_word()%(total+1):0;int index=0;size_t skip=start;
  while(index<n&&skip>=a[index].len){skip-=a[index].len;index++;}
  size_t len=total-start;if(len)len=random_word()%(len+1);
  for(int mode=0;mode<3;mode++){
   for(size_t j=0;j<sizeof(actual);j++)actual[j]=expected[j]=(uint8_t)(j*17+trial);
   for(size_t j=0;j<sizeof(buffer);j++)buffer[j]=reference[j]=(uint8_t)(j*29+trial);
   Cursor x={a,n,index,skip},y={b,n,index,skip};apply(&x,buffer,len,mode);oracle(&y,reference,len,mode);
   assert(!memcmp(actual,expected,sizeof(actual))&&!memcmp(buffer,reference,sizeof(buffer)));
   /* cursor_take also retires following empty entries when seeking the next
    * disjoint run; only compare logical consumed positions. */
   size_t xp=x.off,yp=y.off;for(int i=0;i<x.i;i++)xp+=a[i].len;for(int i=0;i<y.i;i++)yp+=b[i].len;assert(xp==yp&&xp==start+len);
  }
 }
 puts("QCOW2 cursor: 30000 partial/empty/gapped copy-in/copy-out/zero byte and position cases PASS");
}
#ifdef _WIN32
#include <windows.h>
static double now(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
static void bench(void){
 for(int mode=0;mode<3;mode++)for(unsigned layout=0;layout<3;layout++)for(unsigned size=512;size<=65536;size*=size==512?8:16){
  LRFileIOV a[48];int n=layout?16:1;size_t chunk=size/n;
  for(int i=0;i<n;i++)a[i]=(LRFileIOV){actual+i*(chunk+(layout==2?16:0)),chunk};
  void (*volatile operation)(Cursor*,uint8_t*,size_t)=mode==0?apply_out:mode==1?apply_in:apply_zero;
  unsigned loops=size==65536?50000:200000;double begin=now();
  for(unsigned j=0;j<loops;j++){Cursor c={a,n,0,0};operation(&c,buffer,size);__asm__ __volatile__("" : : "r"(actual),"r"(buffer) : "memory");}
  printf("mode=%d layout=%u size=%u ns=%.3f\n",mode,layout,size,(now()-begin)/loops);
 }
}
int main(int argc,char**argv){if(argc>1&&!strcmp(argv[1],"--bench"))bench();else check();return 0;}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
typedef uint64_t hwaddr;
#define TARGET_PAGE_SIZE 4096u
#define TARGET_PAGE_MASK (~(hwaddr)(TARGET_PAGE_SIZE-1))
#define TARGET_PAGE_ALIGN(a) (((a)+TARGET_PAGE_SIZE-1)&TARGET_PAGE_MASK)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define PAGES 1024u
#define BYTES (PAGES*TARGET_PAGE_SIZE)
#define BITS (sizeof(unsigned long)*CHAR_BIT)
#define GL_ARRAY_BUFFER 1
#define DIRTY_MEMORY_NV2A 3
#define NV2A_PROF_GEOM_BUFFER_UPDATE_1 1
static void bitmap_set(unsigned long *b,size_t p,size_t n){for(size_t i=p;i<p+n;i++)b[i/BITS]|=1UL<<(i%BITS);}
static void bitmap_clear(unsigned long *b,size_t p,size_t n){for(size_t i=p;i<p+n;i++)b[i/BITS]&=~(1UL<<(i%BITS));}
static size_t find_next_bit(const unsigned long *b,size_t end,size_t p){while(p<end&&!(b[p/BITS]&(1UL<<(p%BITS))))p++;return p;}
static size_t find_next_zero_bit(const unsigned long *b,size_t end,size_t p){while(p<end&&(b[p/BITS]&(1UL<<(p%BITS))))p++;return p;}
typedef struct {unsigned gl_memory_buffer;unsigned long *vertex_stale_bitmap;size_t vertex_stale_pages;} PGRAPHGLState;
typedef struct {PGRAPHGLState *gl_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;void *vram;uint8_t *vram_ptr;} NV2AState;
static uint8_t dirty[PAGES],mirror[BYTES];
static unsigned calls,profiles,under_rcu,late_page;
static size_t uploaded;
static hwaddr range_start,range_end;
static size_t memory_region_size(void *mr){(void)mr;return BYTES;}
static bool memory_region_test_and_clear_dirty(void *mr,hwaddr a,hwaddr n,unsigned client)
{(void)mr;assert(client==3);bool any=false;for(size_t p=a/4096;p<TARGET_PAGE_ALIGN(a+n)/4096;p++){any|=dirty[p];dirty[p]=0;}return any;}
static void memory_region_set_client_dirty(void *mr,hwaddr a,hwaddr n,unsigned client)
{(void)mr;assert(client==3);memset(dirty+a/4096,1,n/4096);}
static void memory_region_test_and_clear_dirty_ranges(void *mr,hwaddr a,hwaddr n,unsigned c,void (*cb)(void*,hwaddr,hwaddr),void *opaque)
{
 (void)mr;assert(c==3);size_t end=(a+n)/4096;under_rcu=1;
 for(size_t p=a/4096;p<end;){if(!dirty[p]){p++;continue;}size_t first=p;while(p<end&&dirty[p])dirty[p++]=0;cb(opaque,first*4096-a,(p-first)*4096);}
 under_rcu=0;if(late_page){dirty[late_page]=1;late_page=0;}
}
static void glBindBuffer(unsigned target,unsigned buffer){assert(target==1&&buffer==7);}
static void glBufferSubData(unsigned target,hwaddr a,hwaddr n,const void *src)
{assert(target==1&&!under_rcu&&a>=range_start&&a+n<=range_end&&n);memcpy(mirror+a,src,n);calls++;uploaded+=n;}
static void nv2a_profile_inc_counter(unsigned c){assert(c==1);profiles++;}
/* PRODUCTION */
static uint32_t seed=1234567;
static unsigned rng(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static unsigned cases;
static void check(NV2AState *d,hwaddr a,hwaddr n,bool quick)
{
 range_start=a&TARGET_PAGE_MASK;range_end=TARGET_PAGE_ALIGN(a+n);calls=profiles=0;uploaded=0;
 update_memory_buffer(d,a,n,quick);assert(calls<=4&&profiles==(calls?1u:0u));
 for(size_t p=range_start/4096;p<range_end/4096;p++)assert(!memcmp(mirror+p*4096,d->vram_ptr+p*4096,4096));
 cases++;
}
int main(void)
{
 unsigned long stale[PAGES/(sizeof(unsigned long)*CHAR_BIT)]={0};PGRAPHGLState r={7,stale,PAGES};
 uint8_t *ram=calloc(BYTES,1);assert(ram);NV2AState d={{&r},NULL,ram};
 /* Sparse: four dirty pages in 2 MiB, one call per run. */
 for(unsigned p=4;p<400;p+=100){dirty[p]=1;memset(ram+p*4096,p,4096);}
 check(&d,0,512*4096,false);assert(calls==4&&uploaded==4*4096);
 check(&d,0,512*4096,false);assert(!calls);
 /* Fifth run or dense updates retain one bulk call. */
 for(unsigned p=4;p<500;p+=100){dirty[p]=1;ram[p*4096]++;}
 check(&d,0,512*4096,false);assert(calls==1&&uploaded==512*4096);
 memset(dirty,1,400);memset(ram,17,400*4096);check(&d,0,512*4096,false);assert(calls==1&&uploaded==512*4096);
 /* A foreign consumer must not hide writes, including unaligned pages. */
 dirty[44]=1;ram[44*4096]++;assert(pgraph_gl_surface_test_and_clear_dirty(&d,44*4096+17,3));
 assert(!dirty[44]);check(&d,0,512*4096,true);assert(calls==1&&uploaded==4096);
 /* Quick checks are draw-local, preserving a newly written dirty bit. */
 dirty[44]=1;range_start=0;range_end=512*4096;calls=0;update_memory_buffer(&d,0,512*4096,true);assert(!calls&&dirty[44]);check(&d,0,512*4096,false);
 /* A late write remains in shared tracking for the following draw. */
 late_page=55;check(&d,0,512*4096,false);assert(dirty[55]);ram[55*4096]++;check(&d,0,512*4096,false);
 /* Invalidation refreshes lazily, including a small span. */
 pgraph_gl_invalidate_memory_buffer(&d);memset(ram,33,BYTES);check(&d,4096+3,100,false);assert(calls==1&&uploaded==4096);
 check(&d,0,BYTES,false);assert(calls==1&&uploaded==BYTES);
 for(unsigned trial=0;trial<3000;trial++) {
  unsigned first=rng()%800,count=1+rng()%224;
  for(unsigned i=0;i<1+rng()%12;i++){unsigned p=rng()%PAGES;dirty[p]=1;ram[p*4096]++;if((rng()&3)==0)pgraph_gl_surface_test_and_clear_dirty(&d,p*4096+7,1);}
  check(&d,first*4096+17,count*4096-31,false);
 }
 check(&d,0,BYTES,false);
 printf("GL vertex sparse: %u cases; exact mirror bytes, four-run cap, dense/small fallback, shared-bit ownership, reset, quick and late-write preservation PASS\n",cases);
 free(ram);return 0;
}

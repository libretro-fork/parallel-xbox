/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <rthreads/rthreads.h>
typedef struct {int unused;} AioContext;
AioContext *qemu_get_current_aio_context(void);
void initialize_context(void);
typedef struct {uintptr_t map_client_owner;} AddressSpace;
typedef struct {AddressSpace *as;} QEMUSGList;
typedef struct {AioContext *ctx;} Request;
static volatile unsigned work;
static void __attribute__((noinline)) begin(Request *dbs,QEMUSGList *sg){/* BEGIN */ work++;}
static void __attribute__((noinline)) callback(Request *dbs){/* CHECK */ work++;}
#ifdef _WIN32
#include <windows.h>
static double now(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
int main(void){
 initialize_context();AddressSpace as={sthread_get_current_thread_id()};QEMUSGList sg={&as};Request req={0};
 for(unsigned mode=0;mode<3;mode++){
  unsigned callbacks=mode==0?2:mode==1?8:32,loops=1000000;work=0;double start=now();
  for(unsigned n=0;n<loops;n++){begin(&req,&sg);for(unsigned c=0;c<callbacks;c++)callback(&req);}
  assert(work==loops*(1+callbacks));printf("callbacks=%u ns=%.3f\n",callbacks,(now()-start)/loops);
 }
 return 0;
}

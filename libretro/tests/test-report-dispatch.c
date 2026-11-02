/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#define NV_PFIFO_CACHE1_DMA_GET 0
#define NV_PFIFO_CACHE1_DMA_PUT 1
#define VK_FINISH_REASON_STALLED 7
#define QSIMPLEQ_FIRST(q) ((q)->first)
#define QSIMPLEQ_EMPTY(q) (!QSIMPLEQ_FIRST(q))
typedef struct {unsigned query_count;} QueryReport;
typedef struct {
 struct {QueryReport *first;} report_queue;
 bool in_command_buffer;unsigned queries_read,queries_submitted,finished,processed;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;atomic_uint fifo[2];
#ifdef CHECKED
 unsigned reads;
#endif
} NV2AState;
static uint32_t pfifo_reg_r(NV2AState *d,unsigned r){
#ifdef CHECKED
 d->reads++;
#endif
 return atomic_load_explicit(&d->fifo[r],memory_order_relaxed);
}
static void pgraph_vk_finish(PGRAPHState *pg,unsigned reason){assert(reason==VK_FINISH_REASON_STALLED);pg->vk_renderer_state->finished++;}
static void pgraph_vk_process_pending_reports_internal(NV2AState *d){d->pgraph.vk_renderer_state->processed++;}
__attribute__((noinline)) static void reference(NV2AState *d)
/* REFERENCE */
__attribute__((noinline)) static void current(NV2AState *d)
/* CURRENT */
static void correctness(void){
 unsigned cases=0;
 for(unsigned queue=0;queue<2;queue++)for(unsigned open=0;open<2;open++)
 for(unsigned idle=0;idle<2;idle++)for(unsigned read=0;read<8;read++)
 for(unsigned submitted=0;submitted<8;submitted++)for(unsigned count=0;count<8;count++){
  QueryReport head={count};
  PGRAPHVkState a={.report_queue={queue?&head:NULL},.in_command_buffer=open,.queries_read=read,.queries_submitted=submitted},b=a;
  NV2AState da={.pgraph={&a}},db={.pgraph={&b}};
  atomic_init(&da.fifo[0],0);atomic_init(&db.fifo[0],0);
  atomic_init(&da.fifo[1],!idle);atomic_init(&db.fifo[1],!idle);
  reference(&da);current(&db);
  unsigned finish=queue&&open&&idle;
  unsigned process=queue&&!finish&&(read<submitted||count<=read);
  assert(a.finished==finish&&b.finished==finish);
  assert(a.processed==process&&b.processed==process);
  assert(a.report_queue.first==b.report_queue.first);
#ifdef CHECKED
  assert(da.reads==2&&db.reads==(EXPECT_MODE==0?2u:queue&&(EXPECT_MODE==1||open)?2u:0u));
#endif
  cases++;
 }
 printf("%u dispatch cases: oracle submission/processing decisions and FIFO read counts passed\n",cases);
}
#ifndef CHECKED
static volatile unsigned sink;
static double bench(unsigned candidate,unsigned scenario){
 QueryReport head={scenario==4?8:4};PGRAPHVkState r={.report_queue={scenario>=2?&head:NULL},.in_command_buffer=(scenario&1)!=0,.queries_read=2,.queries_submitted=4};
 if(scenario==4){r.queries_read=4;r.queries_submitted=2;}
 NV2AState d={.pgraph={&r}};atomic_init(&d.fifo[0],0);atomic_init(&d.fifo[1],scenario==5?1:0);
 void(*fn)(NV2AState*)=candidate?current:reference;struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<10000000;i++)fn(&d);
 timespec_get(&b,TIME_UTC);sink=r.finished+r.processed;
 return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/10000000;
}
#endif
int main(void){correctness();
#ifndef CHECKED
 puts("Production-vs-reference is an A/A timing control; code layout can affect this tiny benchmark.");
 puts("Scenarios: 0 empty/closed; 1 empty/open; 2 queued/closed/ready; 3 queued/open/idle; 4 queued/closed/not-ready; 5 queued/open/busy.");
 puts("Isolated dispatch: modeled finish/process callbacks, relaxed atomic FIFO loads, ns/call; no GPU timing.");
 for(unsigned round=0;round<5;round++)for(unsigned s=0;s<6;s++){
  double t[2];for(unsigned k=0;k<2;k++){unsigned w=k^(round&1);t[w]=bench(w,s);}
  printf("round=%u scenario=%u baseline=%.3f current=%.3f ns/call\n",round,s,t[0],t[1]);
 }
#endif
}

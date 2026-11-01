/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "qemu/queue.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
static size_t allocations,frees,live;
static void *g_malloc(size_t n){void *p=malloc(n);assert(p);allocations++;live++;return p;}
static void *g_malloc_n(size_t n,size_t size){assert(!size||n<=SIZE_MAX/size);return g_malloc(n*size);}
static void *g_realloc_n(void *p,size_t n,size_t size){assert(p&&n<=SIZE_MAX/size);p=realloc(p,n*size);assert(p);allocations++;return p;}
static void g_free(void *p){if(p){assert(live);live--;frees++;free(p);}}
/* POOL */
typedef unsigned GLuint;
/* REPORT TYPE */
typedef struct {
 ObjPool report_pool;QSIMPLEQ_HEAD(, QueryReport) report_queue;
 QueryReport *zpass_report;bool gl_zpass_query_open,work_since_flush;
 uint32_t zpass_pixel_count_result;
} PGRAPHGLState;
typedef struct {PGRAPHGLState *gl_renderer_state;unsigned surface_scale_factor;bool reports_pending;} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
#define GL_SAMPLES_PASSED 1
#define GL_QUERY_RESULT 2
#define GL_QUERY_RESULT_AVAILABLE 3
#define NV097_GET_REPORT_TYPE 0
#define NV097_GET_REPORT_TYPE_ZPASS_PIXEL_CNT 1
#define GET_MASK(value,mask) ((value)>>24)
static struct {bool exists,ready;GLuint value;} queries[100000];
static GLuint next_id,open_id;static unsigned deleted,flushes,ends,writes;
static uint32_t last_parameter,last_result;
static void glGenQueries(unsigned n,GLuint *out){while(n--){GLuint q=++next_id;assert(q<ARRAY_SIZE(queries));queries[q].exists=true;queries[q].ready=false;queries[q].value=q*73u+19;*out++=q;}}
static void glDeleteQueries(unsigned n,const GLuint *ids){while(n--){GLuint q=*ids++;assert(q&&! (q==open_id)&&queries[q].exists);queries[q].exists=false;deleted++;}}
static void glGetQueryObjectuiv(GLuint q,unsigned what,GLuint *out){assert(q&&q!=open_id&&queries[q].exists);if(what==GL_QUERY_RESULT_AVAILABLE)*out=queries[q].ready;else {assert(what==GL_QUERY_RESULT);*out=queries[q].value;queries[q].ready=true;}}
static void glEndQuery(unsigned target){assert(target==GL_SAMPLES_PASSED&&open_id);open_id=0;ends++;}
static void glFlush(void){flushes++;}
static void pgraph_write_zpass_pixel_cnt_report(NV2AState *d,uint32_t p,uint32_t result){(void)d;writes++;last_parameter=p;last_result=result;}
/* PRODUCTION */
static uint32_t add_query(NV2AState *d,bool ready){GLuint q=pgraph_gl_alloc_zpass_query(d);queries[q].ready=ready;return queries[q].value/(d->pgraph.surface_scale_factor*d->pgraph.surface_scale_factor);}
static void open_query(NV2AState *d){assert(!open_id);open_id=pgraph_gl_alloc_zpass_query(d);d->pgraph.gl_renderer_state->gl_zpass_query_open=true;}
int main(void)
{
 PGRAPHGLState r={0};NV2AState d={{&r,1,false}};pgraph_gl_init_reports(&d);size_t warm=allocations;assert(warm==2);
 uint32_t sum=0;unsigned cases=0;
 /* Pooled inline lists: no query-array allocation, no ownership copy. */
 for(unsigned cycle=0;cycle<1500;cycle++) {
  unsigned count=cycle%9;for(unsigned i=0;i<count;i++)sum+=add_query(&d,true);
  QueryReport *active=r.zpass_report;GLuint *ids=active?active->queries:NULL;
  pgraph_gl_get_report(&d,0x01000000u+cycle);QueryReport *queued=QSIMPLEQ_FIRST(&r.report_queue);
  if(active)assert(queued==active&&queued->queries==ids&&ids==queued->inline_queries);
  unsigned before=writes;pgraph_gl_process_pending_reports(&d);assert(writes==before+1&&last_result==sum&&last_parameter==0x01000000u+cycle);assert(!r.zpass_report&&allocations==warm);cases++;
 }
 /* Overflow grows geometrically, retains IDs, scales each result. */
 unsigned counts[]={9,17,33,129,2049};
 for(unsigned scale=1;scale<=4;scale++)for(unsigned c=0;c<ARRAY_SIZE(counts);c++) {
  d.pgraph.surface_scale_factor=scale;size_t before=allocations;
  for(unsigned i=0;i<counts[c];i++)sum+=add_query(&d,true);
  unsigned growth=0,capacity=8;while(capacity<counts[c]){growth++;capacity*=2;}
  assert(allocations-before==growth&&r.zpass_report->queries!=r.zpass_report->inline_queries);
  QueryReport *active=r.zpass_report;pgraph_gl_get_report(&d,0x01010000+c);assert(QSIMPLEQ_FIRST(&r.report_queue)==active);pgraph_gl_process_pending_reports(&d);assert(last_result==sum&&live==2);cases++;
 }
 d.pgraph.surface_scale_factor=1;
 /* No younger clear/report may pass an unavailable older report. */
 sum+=add_query(&d,false);QueryReport *older=r.zpass_report;GLuint held=older->queries[0];pgraph_gl_get_report(&d,0x01020000);pgraph_gl_clear_report_value(&d);
 uint32_t younger=add_query(&d,true);pgraph_gl_get_report(&d,0x01020001);unsigned before=writes;pgraph_gl_process_pending_reports(&d);assert(writes==before&&QSIMPLEQ_FIRST(&r.report_queue)==older&&d.pgraph.reports_pending);
 unsigned prior_flushes=flushes;for(unsigned i=0;i<1000;i++)assert(pgraph_gl_poll_pending_report(&d)==0&&!queries[held].ready&&writes==before&&flushes==prior_flushes);queries[held].ready=true;assert(pgraph_gl_poll_pending_report(&d)==1);pgraph_gl_process_pending_reports(&d);assert(writes==before+2&&last_result==younger&&!d.pgraph.reports_pending);sum=younger;cases++;
 /* An open, unreported interval is ended and discarded by CLEAR. */
 open_query(&d);unsigned old_deleted=deleted,old_ends=ends;pgraph_gl_clear_report_value(&d);assert(!open_id&&!r.zpass_report&&deleted==old_deleted+1&&ends==old_ends+1);pgraph_gl_process_pending_reports(&d);assert(!r.zpass_pixel_count_result);sum=0;cases++;
 /* Queue pool exhaustion and empty reports preserve order and free fallback. */
 size_t old_allocations=allocations;for(unsigned i=0;i<1030;i++)pgraph_gl_get_report(&d,0x01030000+i);assert(allocations==old_allocations+6);before=writes;pgraph_gl_drain_pending_reports(&d);assert(writes==before+1030&&last_result==0&&live==2&&r.report_pool.nfree==1024);cases++;
 /* Save-state drain resolves unavailable overflow lists, then teardown must
  * free queued overflow, clear nodes and an open active overflow interval. */
 for(unsigned i=0;i<70;i++)sum+=add_query(&d,false);pgraph_gl_get_report(&d,0x01040000);pgraph_gl_drain_pending_reports(&d);assert(last_result==sum&&live==2);cases++;
 for(unsigned i=0;i<19;i++)add_query(&d,false);pgraph_gl_get_report(&d,0x01050000);pgraph_gl_clear_report_value(&d);
 for(unsigned i=0;i<19;i++)add_query(&d,false);open_query(&d);pgraph_gl_finalize_reports(&d.pgraph);assert(!live&&!open_id&&deleted==next_id);cases++;
 pgraph_gl_init_reports(&d);pgraph_gl_get_report(&d,0x01060000);pgraph_gl_process_pending_reports(&d);pgraph_gl_finalize_reports(&d.pgraph);assert(!live&&last_result==sum);cases++;
 printf("GL report storage: %u schedules, %u query lifetimes; pooled inline/overflow ownership, scale, FIFO availability, clear, drain, pool exhaustion, teardown and reinit PASS\n",cases,next_id);
 return 0;
}

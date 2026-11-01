/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define NV2A_MAX_BATCH_LENGTH 0x07FFFF
static size_t allocations,bytes;
static void *g_malloc(size_t n){void *p=malloc(n);assert(p);allocations++;bytes+=n;return p;}
typedef struct {float *inline_buffer;float inline_value[4];bool inline_buffer_populated;} VertexAttribute;
typedef struct {VertexAttribute vertex_attributes[16];unsigned inline_buffer_length;} PGRAPHState;
static void pgraph_check_within_begin_end_block(PGRAPHState *pg){(void)pg;}
/* PRODUCTION */
static void set_value(VertexAttribute *a,unsigned seed)
{uint32_t bits[4]={seed,0x7fc00000u|seed,0x80000000u,0x3f800000u};memcpy(a->inline_value,bits,16);}
int main(void)
{
 PGRAPHState pg={0};
 /* Initial and first-vertex constants allocate nothing. */
 for(unsigned i=0;i<16;i++){set_value(&pg.vertex_attributes[i],i);pgraph_allocate_inline_buffer_vertices(&pg,i);}
 assert(!allocations);for(unsigned i=0;i<90;i++)pgraph_finish_inline_buffer_vertex(&pg);assert(!allocations);
 for(unsigned attr=0;attr<16;attr++) {
  VertexAttribute *a=&pg.vertex_attributes[attr];unsigned before=pg.inline_buffer_length;
  float previous[4];memcpy(previous,a->inline_value,16);
  pgraph_allocate_inline_buffer_vertices(&pg,attr);assert(allocations==attr+1);
  assert(a->inline_buffer_populated&&a->inline_buffer);
  for(unsigned i=0;i<before;i++)assert(!memcmp(a->inline_buffer+i*4,previous,16));
  set_value(a,100+attr);pgraph_finish_inline_buffer_vertex(&pg);
  for(unsigned j=0;j<=attr;j++)assert(!memcmp(pg.vertex_attributes[j].inline_buffer+(pg.inline_buffer_length-1)*4,pg.vertex_attributes[j].inline_value,16));
  float *saved=a->inline_buffer;pgraph_allocate_inline_buffer_vertices(&pg,attr);assert(a->inline_buffer==saved&&allocations==attr+1);
 }
 assert(bytes==(size_t)16*NV2A_MAX_BATCH_LENGTH*16);
 /* A draw flush clears populated, but retains arrays for reuse. */
 for(unsigned batch=0;batch<300;batch++) {
  pg.inline_buffer_length=0;for(unsigned i=0;i<16;i++)pg.vertex_attributes[i].inline_buffer_populated=false;
  pgraph_finish_inline_buffer_vertex(&pg);
  for(unsigned i=0;i<16;i++){pgraph_allocate_inline_buffer_vertices(&pg,i);assert(!memcmp(pg.vertex_attributes[i].inline_buffer,pg.vertex_attributes[i].inline_value,16));set_value(&pg.vertex_attributes[i],batch*16+i);}
  for(unsigned v=0;v<127;v++)pgraph_finish_inline_buffer_vertex(&pg);
  for(unsigned i=0;i<16;i++)for(unsigned v=1;v<128;v++)assert(!memcmp(pg.vertex_attributes[i].inline_buffer+v*4,pg.vertex_attributes[i].inline_value,16));
  assert(allocations==16);
 }
 /* Maximum-capacity backfill and final legal vertex: no growth copies. */
 for(unsigned i=0;i<16;i++)pg.vertex_attributes[i].inline_buffer_populated=false;
 pg.inline_buffer_length=NV2A_MAX_BATCH_LENGTH-1;
 pgraph_allocate_inline_buffer_vertices(&pg,0);pgraph_finish_inline_buffer_vertex(&pg);
 for(unsigned v=0;v<NV2A_MAX_BATCH_LENGTH;v++)assert(!memcmp(pg.vertex_attributes[0].inline_buffer+v*4,pg.vertex_attributes[0].inline_value,16));
 assert(allocations==16);for(unsigned i=0;i<16;i++)free(pg.vertex_attributes[i].inline_buffer);
 printf("Inline vertex storage: zero constant-path allocations, 16 first-use allocations, 300 reused batches and %u-vertex boundary; exact float/NaN bits PASS\n",NV2A_MAX_BATCH_LENGTH);
 return 0;
}

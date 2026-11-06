/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define BUSY_STAT 128
#define IDE_CD 1
#define IDE_DMA_ATAPI 3
#define g_new0(t,n) ((t*)calloc(n,sizeof(t)))
/* TYPES */
typedef void BlockCompletionFunc(void*,int);
typedef void BlockAIOCB;
typedef struct IDEState IDEState;
typedef struct {uint64_t base,len;} SG;
typedef struct {SG*sg;int nsg;uint64_t size;} QEMUSGList;
typedef struct {void*base;size_t len;} QEMUIOVector;
typedef struct DMA DMA;
struct DMA {struct {void(*restore_dma)(DMA*,IDEState*,BlockCompletionFunc*);}*ops;BlockAIOCB*aiocb;QEMUIOVector qiov;};
typedef struct {DMA*dma;} Bus;
struct IDEState {IDEDMAReplay*dma_replay;bool atapi_dma_buffered,atapi_dma_direct;int drive_kind,status,lba,packet_transfer_size,cd_sector_size,io_buffer_size,io_buffer_index,atapi_dma,dma_cmd,io_buffer_total_len;uint64_t nb_sectors;QEMUSGList sg;Bus*bus;void*blk;uint8_t*io_buffer;};
static uint64_t saved_offset=4096,saved_done=713;
static unsigned submits,restores,errors;static bool view_available=true;
static void*handle=(void*)(uintptr_t)17;
static bool dma_read_view_progress(void*a,uint64_t*offset,uint64_t*done){if(!a)return false;*offset=saved_offset;*done=saved_done;return true;}
static void ide_atapi_cmd_read_dma_cb(void*a,int ret){(void)a;(void)ret;}
static void ide_atapi_cmd_read_dma_direct_cb(void*a,int ret){(void)a;(void)ret;}
static void qemu_iovec_init_buf(QEMUIOVector*q,void*p,size_t n){q->base=p;q->len=n;}
static void*ide_buffered_readv(IDEState*s,int64_t sector,QEMUIOVector*q,int count,BlockCompletionFunc*cb,void*opaque){assert(opaque==s && sector==(int64_t)s->lba*4 && q->len==(size_t)count*512 && cb==ide_atapi_cmd_read_dma_cb);submits++;return handle;}
static void*dma_read_view_resume(void*blk,QEMUSGList*sg,uint64_t offset,uint64_t done,BlockCompletionFunc*cb,void*opaque){(void)blk;IDEState*s=opaque;assert(sg==&s->sg && offset==4096 && done==713 && cb==ide_atapi_cmd_read_dma_direct_cb);submits++;return view_available?handle:NULL;}
static void ide_dma_error(IDEState*s){(void)s;errors++;}
static void restore(DMA*d,IDEState*s,BlockCompletionFunc*cb){assert(d==s->bus->dma && cb==(s->dma_replay->buffered?ide_atapi_cmd_read_dma_cb:ide_atapi_cmd_read_dma_direct_cb));restores++;}
/* PRODUCTION */
static typeof(*((DMA*)0)->ops) ops={restore};static DMA dma;static Bus bus;static IDEState state;static SG entries[2]={{100,1024},{2000,3072}};static uint8_t buffer[131104];
static void setup(bool buffered){
 free(state.dma_replay);dma=(DMA){.ops=&ops,.aiocb=handle};bus=(Bus){&dma};
 state=(IDEState){.bus=&bus,.drive_kind=IDE_CD,.status=BUSY_STAT,.lba=2,.packet_transfer_size=4096,.cd_sector_size=2048,.io_buffer_size=4096,.io_buffer_total_len=sizeof(buffer),.io_buffer=buffer,.nb_sectors=32768,.sg={entries,2,4096},.atapi_dma_direct=!buffered,.atapi_dma_buffered=buffered};
 submits=restores=errors=0;view_available=true;
 assert(ide_dma_replay_needed(&state));assert(!ide_dma_replay_pre_save(&state));assert(!submits && !state.dma_replay->pending);
}
int main(void){
 for(unsigned raw=0;raw<3;raw++){
  setup(raw!=0);if(raw==2){state.dma_replay->sector_size=2352;state.dma_replay->packet=2352*5;state.dma_replay->buffer_size=2352;}
  IDEDMAReplay saved=*state.dma_replay;
  assert(!ide_dma_replay_alloc(&state));assert(!state.dma_replay->pending);*state.dma_replay=saved;
  dma.aiocb=NULL;state.atapi_dma_direct=state.atapi_dma_buffered=false;
  assert(!ide_dma_replay_post_load(&state,1) && state.dma_replay->pending && !submits);
  ide_atapi_dma_resume(&state);assert(!state.dma_replay->pending && submits==1 && restores==1 && dma.aiocb==handle);
  if(raw==2)assert(dma.qiov.base==buffer+16 && dma.qiov.len==2048);
  for(int i=0;i<20;i++){ide_atapi_dma_resume(&state);}
  assert(submits==1 && restores==1);
 }
 for(unsigned bad=0;bad<14;bad++){
  setup(false);IDEDMAReplay*r=state.dma_replay;
  switch(bad){case 0:r->count=513;break;case 1:r->count=0;break;case 2:r->done=4096;break;case 3:r->offset++;break;case 4:r->lba=-1;break;case 5:r->packet=0;break;case 6:r->sector_size=2352;break;case 7:r->length[0]=0;break;case 8:r->length[0]=65537;break;case 9:r->base[0]=UINT64_MAX;break;case 10:r->packet=2048;break;case 11:state.status=0;break;case 12:state.nb_sectors=1;break;case 13:state.drive_kind=0;break;}
  assert(ide_dma_replay_post_load(&state,1)==-EINVAL && !r->pending && !submits);
  ide_atapi_dma_resume(&state);assert(!submits);
 }
 setup(true);state.dma_replay->buffer_size=2352;assert(ide_dma_replay_post_load(&state,1)==-EINVAL);
 setup(true);state.dma_replay->done=1;assert(ide_dma_replay_post_load(&state,1)==-EINVAL);
 setup(false);dma.aiocb=NULL;assert(!ide_dma_replay_post_load(&state,1));view_available=false;ide_atapi_dma_resume(&state);assert(errors==1 && !state.dma_replay->pending);
 free(state.dma_replay);puts("ATAPI DMA replay: exact parsed SG/progress, cooked/raw buffered extents, malformed states, deferred resume and failure retirement PASS");
}

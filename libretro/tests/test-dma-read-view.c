/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define DMA_DIRECTION_FROM_DEVICE 1
#define MEMTXATTRS_UNSPECIFIED 0
#define MAX_IDE_DEVS 2
#define IMAGE 65536
#define RAM 16384
typedef uint64_t dma_addr_t;
typedef struct BlockAIOCB BlockAIOCB;
typedef void BlockCompletionFunc(void*,int);
typedef struct {size_t aiocb_size;void(*cancel_async)(BlockAIOCB*);} AIOCBInfo;
struct BlockAIOCB {const AIOCBInfo*aiocb_info;BlockCompletionFunc*cb;void*opaque;int refs;};
typedef struct AddressSpaceMapClient {void(*cb)(void*);void*opaque;} AddressSpaceMapClient;
typedef struct {uintptr_t map_client_owner;AddressSpaceMapClient*client;} AddressSpace;
typedef struct {dma_addr_t base,len;} ScatterGatherEntry;
typedef struct {ScatterGatherEntry*sg;int nsg;uint64_t size;AddressSpace*as;} QEMUSGList;
enum {LR_MEDIUM_NONE,LR_MEDIUM_FILE,LR_MEDIUM_OTHER};
typedef struct {struct {int kind;bool writable;int64_t length;uint64_t base;void*file;} m;} BlockBackend;
static uint8_t image[IMAGE],ram[RAM],oracle[RAM];static BlockBackend backend;
static AddressSpace as;static QEMUSGList list;
typedef struct IDEBus IDEBus;
typedef struct {bool atapi_dma_direct,atapi_pio_pending,tray_open;unsigned commits,aborts,inactive,unit,cdrom_changed;uint64_t nb_sectors;BlockBackend*blk;IDEBus*bus;struct {bool new_media,eject_request;} events;} IDEState;
typedef void Error;
struct IDEBus {uint8_t retry_unit,unit;IDEState ifs[2];struct {BlockAIOCB*aiocb;} *dma;unsigned irqs;};
static typeof(*((IDEBus*)0)->dma) dma;
static IDEBus bus;
static bool ide_abandon_dma_view(IDEBus*);
static BlockAIOCB*active;static unsigned allocated,live,refs,maps,mapped,unmaps,dirty,callbacks,submits;
static size_t cap,map_piece,block_after;static int result,source_error,cancel_mode,refuse_at;
static bool blocked,mapping_enabled,register_inline,ready_cancel,cancel_self;
static void retire_descriptors(void){free(list.sg);list.sg=NULL;list.nsg=0;list.size=0;}
static uintptr_t sthread_get_current_thread_id(void){return 17;}
static const uint8_t*lr_file_view(void*f,int64_t off,size_t n){(void)f;return mapping_enabled && off>=0 && off<=IMAGE && n<=IMAGE-(size_t)off?image+off:NULL;}
/* QUERY */
static size_t lr_disk_read_view_max(BlockBackend*b,uint64_t off,uint64_t n){size_t x=actual_view_limit(b,off,n);return MIN(x,cap);}
static void *blk_aio_get(const AIOCBInfo*i,BlockBackend*b,BlockCompletionFunc*cb,void*arg){(void)b;BlockAIOCB*a=calloc(1,i->aiocb_size);assert(a);a->aiocb_info=i;a->cb=cb;a->opaque=arg;a->refs=1;allocated++;live++;return a;}
static void qemu_aio_unref(BlockAIOCB*a){assert(a->refs>0);if(!--a->refs){live--;free(a);}}
bool dma_abandon_read_view(BlockAIOCB*);
static void blk_aio_cancel(BlockAIOCB*);
static void cancel_now(int mode){if(mode>=3){assert(ide_abandon_dma_view(&bus));}else blk_aio_cancel(active);}
static void lr_disk_release_read_view(void*owner){assert(owner==&refs && refs);refs--;if(cancel_mode==5 || cancel_mode==6){int m=cancel_mode;cancel_mode=0;cancel_now(m);}}
typedef struct {BlockAIOCB a;bool busy,dispatch;uint64_t off;size_t n;const void**data;void**owner;} Source;
static Source queue[8];static const AIOCBInfo source_info={0};
static BlockAIOCB*lr_disk_aio_read_view(BlockBackend*b,int64_t off,size_t n,const void**data,void**owner,BlockCompletionFunc*cb,void*arg){
 (void)b;*data=NULL;*owner=NULL;submits++;
 if(refuse_at==(int)submits)return NULL;
 assert(off>=0 && (uint64_t)off<=IMAGE && n<=IMAGE-(uint64_t)off);
 for(unsigned i=0;i<8;i++)if(!queue[i].busy){Source*p=&queue[i];*p=(Source){.a={&source_info,cb,arg,1},.busy=true,.off=(uint64_t)off,.n=n,.data=data,.owner=owner};return &p->a;}
 abort();
}
static void deliver(Source*p,int ret){assert(p->busy&&!p->dispatch);p->dispatch=true;
 if(!ret){*p->data=image+p->off;*p->owner=&refs;refs++;}
 BlockCompletionFunc*cb=p->a.cb;void*arg=p->a.opaque;cb(arg,ret);p->busy=p->dispatch=false;
}
static void blk_aio_cancel(BlockAIOCB*a){if(a->aiocb_info==&source_info)deliver((Source*)a,ready_cancel?0:-ECANCELED);else a->aiocb_info->cancel_async(a);}
static void *dma_memory_map(AddressSpace*a,dma_addr_t addr,dma_addr_t*n,int dir,int attrs){
 assert(a==&as && dir==1 && attrs==0 && refs && !mapped);maps++;
 if(blocked || dirty>=block_after){*n=0;return NULL;}
 assert(addr<=RAM && *n<=RAM-addr);*n=MIN(*n,map_piece);*n=MIN(*n,block_after-dirty);assert(*n);mapped++;
 if(cancel_mode==1 || cancel_mode==3){int m=cancel_mode;cancel_mode=0;cancel_now(m);}
 return ram+addr;
}
static void dma_memory_unmap(AddressSpace*a,void*p,dma_addr_t n,int dir,dma_addr_t access){
 assert(a==&as && (uint8_t*)p>=ram && (uint8_t*)p+n<=ram+RAM && dir==1 && mapped==1 && access<=n);
 mapped--;unmaps++;dirty+=(unsigned)access;
 if(cancel_mode==2 || cancel_mode==4){int m=cancel_mode;cancel_mode=0;cancel_now(m);}
}
static void wake_map(void){AddressSpaceMapClient*c=as.client;assert(c);as.client=NULL;void(*cb)(void*)=c->cb;void*arg=c->opaque;cb(arg);}
static void address_space_register_map_client(AddressSpace*a,AddressSpaceMapClient*c,void(*cb)(void*),void*arg){assert(a==&as && !a->client && !mapped);c->cb=cb;c->opaque=arg;a->client=c;if(register_inline){register_inline=false;blocked=false;block_after=SIZE_MAX;wake_map();}}
static void address_space_unregister_map_client(AddressSpace*a,AddressSpaceMapClient*c){assert(a==&as && a->client==c);a->client=NULL;}
#include "system/dma-read-view.inc"
static void ide_dma_buf_commit(IDEState*s,unsigned n){assert(s==&bus.ifs[1] && !n);s->commits++;retire_descriptors();}
static void ide_abort_command(IDEState*s){s->aborts++;}
static void ide_set_inactive(IDEState*s,bool more){assert(!more);s->inactive++;}
/* IDE HELPER */
static void ide_atapi_pio_cancel(IDEState*s){s->atapi_pio_pending=false;}
static void blk_get_geometry(BlockBackend*b,uint64_t*n){assert(b==&backend);*n=(uint64_t)b->m.length/512;}
static void ide_bus_set_irq(IDEBus*b){b->irqs++;}
/* MEDIA CALLBACK */
static void complete(void*arg,int ret){assert(arg==&callbacks && !mapped);callbacks++;result=ret;dma.aiocb=NULL;if(cancel_self)blk_aio_cancel(active);}
static void reset(size_t limit){
 assert(!live && !refs && !mapped && !as.client);for(unsigned i=0;i<8;i++)assert(!queue[i].busy);
 memset(ram,0xcc,sizeof(ram));memcpy(oracle,ram,sizeof(ram));
 for(unsigned i=0;i<IMAGE;i++)image[i]=(uint8_t)((i*29)^(i>>7));
 backend=(BlockBackend){.m={LR_MEDIUM_FILE,false,IMAGE,0,NULL}};as=(AddressSpace){.map_client_owner=17};
 allocated=maps=unmaps=dirty=callbacks=submits=0;cap=limit;map_piece=127;block_after=SIZE_MAX;
 blocked=register_inline=ready_cancel=cancel_self=false;mapping_enabled=true;result=source_error=cancel_mode=refuse_at=0;
 ScatterGatherEntry entries[]={{100,512},{200,1024},{0,0},{3000,2048},{100,512}};
 list=(QEMUSGList){.sg=malloc(sizeof(entries)),.nsg=5,.size=4096,.as=&as};assert(list.sg);memcpy(list.sg,entries,sizeof(entries));
 size_t off=512;for(int i=0;i<list.nsg;i++){memcpy(oracle+list.sg[i].base,image+off,list.sg[i].len);off+=list.sg[i].len;}
 bus=(IDEBus){.retry_unit=1,.unit=0,.dma=&dma};bus.ifs[1].atapi_dma_direct=true;
 for(unsigned i=0;i<2;i++){bus.ifs[i].bus=&bus;bus.ifs[i].unit=i;bus.ifs[i].blk=&backend;}
 active=dma_read_view_start(&backend,&list,512,512,complete,&callbacks);assert(active && live==1 && !maps && !refs);dma.aiocb=active;
}
static void finish_sources(void){unsigned loops=0;while(live){bool ran=false;for(unsigned i=0;i<8;i++)if(queue[i].busy&&!queue[i].dispatch){assert(!mapped);deliver(&queue[i],source_error);ran=true;}assert(++loops<100 && ran);}}
static void done(void){assert(!live && !refs && !mapped && !as.client);if(list.sg)retire_descriptors();}
static uint32_t random_state=7919;
static unsigned next_random(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void randomized(void){
 for(unsigned test=0;test<1000;test++){
  reset(test%4==3?SIZE_MAX:(size_t)512<<(test%4));blk_aio_cancel(active);done();
  callbacks=dirty=maps=unmaps=0;result=0;map_piece=1+next_random()%255;
  memset(ram,0xcc,RAM);memcpy(oracle,ram,RAM);
  int count=1+next_random()%15;list=(QEMUSGList){.sg=calloc(17,sizeof(*list.sg)),.nsg=count+1,.as=&as};assert(list.sg);
  for(int i=0;i<count;i++){list.sg[i].len=(next_random()%1025)&~1u;list.sg[i].base=next_random()%(RAM-1024);list.size+=list.sg[i].len;}
  if(!list.size){list.sg[0].len=list.size=512;}
  list.sg[count].base=7000;list.sg[count].len=(-list.size)&511;list.size+=list.sg[count].len;
  size_t off=512;for(int i=0;i<list.nsg;i++){memcpy(oracle+list.sg[i].base,image+off,list.sg[i].len);off+=list.sg[i].len;}
  active=dma_read_view_start(&backend,&list,512,512,complete,&callbacks);assert(active);dma.aiocb=active;
  finish_sources();assert(callbacks==1 && !result && dirty==list.size && !memcmp(ram,oracle,RAM));done();
 }
}
int main(void){
 for(unsigned chunk=0;chunk<2;chunk++){
  reset(chunk?512:SIZE_MAX);assert(!callbacks);finish_sources();assert(callbacks==1 && !result && !memcmp(ram,oracle,RAM));assert(submits==(chunk?8u:1u));done();
 }
 reset(1024);blocked=true;deliver(&queue[0],0);assert(as.client && refs==1 && !mapped && !callbacks && !dirty);
 blocked=false;wake_map();finish_sources();assert(!result && callbacks==1 && !memcmp(ram,oracle,RAM));done();
 reset(1024);block_after=200;deliver(&queue[0],0);assert(as.client && dirty==200 && !mapped);
 block_after=SIZE_MAX;wake_map();finish_sources();assert(callbacks==1 && !result && !memcmp(ram,oracle,RAM));done();
 reset(SIZE_MAX);blocked=true;register_inline=true;deliver(&queue[0],0);finish_sources();assert(callbacks==1 && !result);done();
 for(unsigned ready=0;ready<2;ready++){reset(1024);ready_cancel=ready;blk_aio_cancel(active);assert(callbacks==1 && result==-ECANCELED && !maps && !dirty);done();}
 reset(1024);blocked=true;deliver(&queue[0],0);blk_aio_cancel(active);assert(callbacks==1 && result==-ECANCELED && !mapped && !dirty);done();
 for(unsigned mode=1;mode<=5;mode++){
  reset(1024);cancel_mode=mode;deliver(&queue[0],0);
  assert(!live && callbacks==(mode>=3?0u:1u));if(mode<3)assert(result==-ECANCELED);
  else assert(!dma.aiocb && !bus.ifs[1].atapi_dma_direct && bus.ifs[1].commits==1 && bus.ifs[1].aborts==1 && bus.ifs[1].inactive==1 && !bus.ifs[0].commits);
  if(mode==1 || mode==3)assert(!dirty);
  done();
 }
 reset(1024);cancel_mode=6;ready_cancel=true;blk_aio_cancel(active);assert(!callbacks && !dirty);done();
 reset(1024);assert(dma_abandon_read_view(active));retire_descriptors();assert(!callbacks && !maps);done();
 reset(1024);blocked=true;deliver(&queue[0],0);assert(dma_abandon_read_view(active));retire_descriptors();assert(!callbacks && !mapped);done();
 reset(1024);cancel_self=true;finish_sources();assert(callbacks==1 && !result);done();
 reset(1024);list.nsg=-1;finish_sources();assert(callbacks==1 && result==-EINVAL && !maps);done();
 reset(1024);source_error=-EIO;finish_sources();assert(callbacks==1 && result==-EIO && !maps);done();
 reset(1024);refuse_at=2;finish_sources();assert(callbacks==1 && result==-EIO && dirty==1024);done();
 reset(1024);blk_aio_cancel(active);done();allocated=0;backend.m.writable=true;
 assert(!dma_read_view_start(&backend,&list,512,512,complete,&callbacks) && !allocated);
 assert(!actual_view_limit(&backend,512,4096));backend.m.writable=false;
 assert(!actual_view_limit(&backend,UINT64_MAX,1));assert(!actual_view_limit(&backend,IMAGE-1,2));
 backend.m.base=INT64_MAX;assert(!actual_view_limit(&backend,1,1));backend.m.base=0;
 mapping_enabled=false;assert(!actual_view_limit(&backend,512,4096));mapping_enabled=true;
 assert(actual_view_limit(&backend,512,4096)==SIZE_MAX);
 /* No retained-view allocation or callback on an initial provider refusal. */
 reset(1024);blk_aio_cancel(active);done();
 ScatterGatherEntry one={0,512};list=(QEMUSGList){&one,1,512,&as};refuse_at=(int)submits+1;
 unsigned before=allocated;assert(!dma_read_view_start(&backend,&list,512,512,complete,&callbacks));assert(allocated==before+1 && !live && !refs);
 /* Tray changes retire only the affected device, before source replacement. */
 reset(1024);ide_cd_change_cb(&bus.ifs[0],false,NULL);
 assert(live==1 && dma.aiocb==active && !bus.ifs[1].aborts);
 finish_sources();assert(callbacks==1 && !result);done();
 for(unsigned waiting=0;waiting<2;waiting++){
  reset(1024);if(waiting){blocked=true;deliver(&queue[0],0);assert(as.client);}
  ide_cd_change_cb(&bus.ifs[1],false,NULL);
  assert(!live && !callbacks && !dirty && !dma.aiocb && bus.ifs[1].aborts==1 && bus.ifs[1].tray_open && bus.irqs==1);done();
 }
 reset(1024);blk_aio_cancel(active);done();bus.ifs[1].atapi_pio_pending=true;
 ide_cd_change_cb(&bus.ifs[1],false,NULL);assert(!bus.ifs[1].atapi_pio_pending && bus.ifs[1].aborts==1);
 ide_cd_change_cb(&bus.ifs[1],true,NULL);assert(!bus.ifs[1].tray_open && bus.ifs[1].aborts==1);
 randomized();
 puts("DMA retained source: no early maps, ordered overlaps/chunks, map retries, precise unmaps, cancellation/reentry, abandonment and range/fallback oracles PASS");
}

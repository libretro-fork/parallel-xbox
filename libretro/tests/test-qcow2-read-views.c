/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "block/libretro-file.c"
#include "block/libretro-qcow2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "qemu/queue.h"
static void *scratch[64];static size_t capacities[64];
static bool watching,fail_decode_alloc;
static const uint8_t *patch_source;
static uint8_t *patch_destination;
static size_t patch_preserved_bytes,patch_cluster_size;
static bool fail_patch_write;
static int observed_write(LRFile *,int64_t,const void *,size_t);
static size_t scratch_copy_bytes;static unsigned decode_allocations;
static void *observed_copy(void *dst,const void *src,size_t n)
{
 if(watching)for(unsigned i=0;i<64;i++)if(scratch[i] && (uintptr_t)dst>=(uintptr_t)scratch[i] && (uintptr_t)dst-(uintptr_t)scratch[i]<capacities[i])scratch_copy_bytes+=n;
 if(patch_source && (uintptr_t)src >= (uintptr_t)patch_source &&
    (uintptr_t)src-(uintptr_t)patch_source < patch_cluster_size &&
    (uintptr_t)dst >= (uintptr_t)patch_destination &&
    (uintptr_t)dst-(uintptr_t)patch_destination < patch_cluster_size)patch_preserved_bytes+=n;
 return memcpy(dst,src,n);
}
static void *observed_malloc(size_t n)
{if(watching){decode_allocations++;if(fail_decode_alloc)return NULL;}return malloc(n);}
#define memcpy observed_copy
#define malloc observed_malloc
#define lr_file_pwrite observed_write
#include "block/libretro-qcow2.c"
#undef memcpy
#undef malloc
#undef lr_file_pwrite
static int observed_write(LRFile *f,int64_t off,const void *buf,size_t n)
{if(fail_patch_write && buf==patch_destination)return -EIO;return lr_file_pwrite(f,off,buf,n);}
#define BDRV_SECTOR_BITS 9
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define QEMU_BUILD_BUG_ON(x) _Static_assert(!(x), #x)
#ifdef _WIN32
struct iovec { void *iov_base; size_t iov_len; };
#endif
/* QIOV */
typedef void BlockCompletionFunc(void *,int);
typedef struct {BlockCompletionFunc *cb;void *opaque;int ret;bool live;} BlockAIOCB;
/* REQUEST */
typedef struct {struct {int kind;LRQcow2 *qcow2;LRFile *file;uint64_t base;int64_t length;bool writable;} m;} BlockBackend;
#define LR_MEDIUM_QCOW2 2
#define LR_MEDIUM_FILE 1
static BlockAIOCB pending[64];static unsigned queued,legacy_reads,releases,calls,req_live,scratch_live;
static BlockAIOCB *lr_disk_submit(BlockBackend *blk,int ret,BlockCompletionFunc *cb,void *opaque)
{for(unsigned i=0;i<64;i++)if(!pending[i].live){pending[i]=(BlockAIOCB){cb,opaque,ret,true};queued++;return &pending[i];}abort();}
/* BLOCK */
static BlockAIOCB *blk_aio_preadv(BlockBackend *blk,int64_t off,QEMUIOVector *v,int flags,BlockCompletionFunc *cb,void *opaque)
{legacy_reads++;
 int ret=blk->m.kind==LR_MEDIUM_QCOW2?
 lr_qcow2_preadv(blk->m.qcow2,off,(LRFileIOV*)v->iov,v->niov):
 lr_file_preadv(blk->m.file,blk->m.base+off,(LRFileIOV*)v->iov,v->niov);
 return lr_disk_submit(blk,ret,cb,opaque);}
static BlockAIOCB *blk_abort_aio_request(BlockBackend *blk,BlockCompletionFunc *cb,void *opaque,int ret)
{return lr_disk_submit(blk,ret,cb,opaque);}
static void finish(BlockAIOCB *a){assert(a->live);a->live=false;releases++;a->cb(a->opaque,a->ret);}
static void *req_new(size_t n){req_live++;void*p=calloc(1,n);assert(p);return p;}
static void req_free(void*p){if(p){assert(req_live);req_live--;free(p);}}
#define g_new0(t,n) ((t*)req_new(sizeof(t)*(n)))
#define g_free req_free
static void *blk_blockalign(BlockBackend *blk,size_t n)
{
 void *p;
#ifdef _WIN32
 p=_aligned_malloc(n?n:1,4096);
#else
 assert(!posix_memalign(&p,4096,n?n:1));
#endif
 assert(p);for(unsigned i=0;i<64;i++)if(!scratch[i]){scratch[i]=p;capacities[i]=n;scratch_live++;return p;}abort();
}
static void qemu_vfree(void*p)
{
 if(!p)return;
 for(unsigned i=0;i<64;i++)if(scratch[i]==p){scratch[i]=NULL;capacities[i]=0;scratch_live--;
#ifdef _WIN32
 _aligned_free(p);
#else
 free(p);
#endif
 return;}abort();
}
static size_t qemu_iovec_from_buf(QEMUIOVector *v,size_t off,const void *src,size_t size)
{assert(!off&&size==v->size);size_t left=size;for(int i=0;i<v->niov;i++){size_t n=v->iov[i].iov_len<left?v->iov[i].iov_len:left;memcpy(v->iov[i].iov_base,src,n);src=(const uint8_t*)src+n;left-=n;}assert(!left);return size;}
typedef struct {BlockAIOCB *aiocb;} DMA;
typedef struct {DMA *dma;} IDEBus;
typedef struct {QLIST_HEAD(,IDEBufferedRequest) buffered_requests;BlockBackend *blk;IDEBus *bus;} IDEState;
static void blk_drain(BlockBackend *blk){for(unsigned i=0;i<64;i++)if(pending[i].live)finish(&pending[i]);}
#define trace_ide_cancel_dma_sync_buffered(cb,req) ((void)0)
#define trace_ide_cancel_dma_sync_remaining() ((void)0)
/* IDE */
static void completed(void *opaque,int ret){assert(ret==*(int*)opaque);calls++;}
static void pattern(uint8_t *out,size_t n,unsigned gc){for(size_t i=0;i<n;i++)out[i]=(uint8_t)(i/17+i%13+gc*37);}
static void mapped_cases(const char *path)
{
 LRFile *file=lr_file_open(path,false,false);assert(file);
 int64_t size=lr_file_size(file);assert(size>2048);
 uint8_t *mapping=malloc(size);assert(mapping);
 assert(!lr_file_pread(file,0,mapping,size));file->mapped=mapping;file->mapped_size=size;
 BlockBackend blk={.m={.kind=LR_MEDIUM_FILE,.file=file,.base=37,.length=size-37}};
 DMA dma={0};IDEBus bus={&dma};IDEState state={.blk=&blk,.bus=&bus};QLIST_INIT(&state.buffered_requests);
 uint8_t out[1100];memset(out,0xcc,sizeof(out));int ok=0,canceled=-ECANCELED;
 struct iovec pieces[3]={{out,173},{out+173,0},{out+200,339}};
 QEMUIOVector a={.iov=pieces,.niov=3,.size=512},b;
 qemu_iovec_init_buf(&b,out+550,512);
 const void *data=(void*)1;void *owner=(void*)1;
 assert(!lr_disk_aio_read_view(&blk,-1,512,&data,&owner,completed,&ok)&&!data&&!owner);
 assert(!lr_disk_aio_read_view(&blk,blk.m.length,1,&data,&owner,completed,&ok));
 assert(!lr_disk_aio_read_view(&blk,0,0,&data,&owner,completed,&ok));
 blk.m.writable=true;assert(!lr_disk_aio_read_view(&blk,0,512,&data,&owner,completed,&ok));blk.m.writable=false;
 uint64_t base=blk.m.base;blk.m.base=UINT64_MAX;
 assert(!lr_disk_aio_read_view(&blk,0,512,&data,&owner,completed,&ok));blk.m.base=base;
 unsigned reads=legacy_reads;BlockAIOCB *first=ide_buffered_readv(&state,0,&a,1,completed,&ok);
 BlockAIOCB *second=ide_buffered_readv(&state,1,&b,1,completed,&ok);
 assert(file->views==2&&legacy_reads==reads&&!scratch_live);
 IDEBufferedRequest *req=first->opaque;assert(req->read_view==mapping+37);
 for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0xcc);
 lr_file_close(file);assert(file->closed&&file->views==2);
 finish(second);assert(file->views==1);finish(first);
 assert(!memcmp(out,mapping+37,173)&&!memcmp(out+200,mapping+37+173,339));
 assert(!memcmp(out+550,mapping+37+512,512));
 for(unsigned i=173;i<200;i++)assert(out[i]==0xcc);
 ide_buffered_read_cache_clear();assert(!req_live&&!scratch_live);
 /* Close followed by cancellation must release the mapping without publication. */
 file=lr_file_open(path,false,false);assert(file);file->mapped=mapping;file->mapped_size=size;blk.m.file=file;
 memset(out,0xcc,sizeof(out));first=ide_buffered_readv(&state,0,&a,1,completed,&canceled);
 lr_file_close(file);unsigned before=calls;ide_cancel_dma_sync(&state);assert(calls==before+1);
 finish(first);assert(calls==before+1);
 for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0xcc);
 ide_buffered_read_cache_clear();assert(!req_live&&!scratch_live);
 /* Unmapped native media retains scratch completion. */
 file=lr_file_open(path,false,false);assert(file);blk.m.file=file;
 first=ide_buffered_readv(&state,0,&a,1,completed,&ok);assert(legacy_reads==reads+1&&scratch_live==1);
 finish(first);lr_file_close(file);ide_buffered_read_cache_clear();
 assert(!req_live&&!scratch_live);free(mapping);
 puts("Mapped raw IDE views: partition offset, concurrent/scattered reads, delayed publication, close, cancellation and fallback PASS");
}
static void mapped_qcow_cases(const char *path)
{
 LRFile *file=lr_file_open(path,false,false);assert(file);int64_t size=lr_file_size(file);
 uint8_t *mapping=malloc(size);assert(mapping);assert(!lr_file_pread(file,0,mapping,size));
 file->mapped=mapping;file->mapped_size=size;char error[256];
 LRQcow2 *q=lr_qcow2_open(file,false,error,sizeof(error));assert(q);
 size_t cs=q->cs;uint8_t *out=malloc(cs*2),*expected=malloc(cs*2);assert(out&&expected);
 pattern(expected,cs,0);memset(expected+117,0x7a,13);pattern(expected+cs,cs,1);
 assert(!(*l2_entry(q,0)&F_COMPRESSED));
 LRQcow2ReadView direct;assert(lr_qcow2_read_view(q,17,73,&direct)==1);
 assert(direct.owner==file&&direct.data==mapping+(*l2_entry(q,0)&OFFSET_MASK)+17);
 assert(!memcmp(direct.data,expected+17,73));lr_qcow2_release_read_view(direct.owner);
 uint64_t entry=*l2_entry(q,0);*l2_entry(q,0)=0;
 assert(!lr_qcow2_read_view(q,0,512,&direct));
 if(q->version==3){*l2_entry(q,0)=entry|F_ZERO;assert(!lr_qcow2_read_view(q,0,512,&direct));}
 *l2_entry(q,0)=entry;
 /* Incomplete mappings fall back, including short physical spans. */
 int64_t mapped_size=file->mapped_size;file->mapped_size=(entry&OFFSET_MASK)+511;
 assert(!lr_qcow2_read_view(q,0,512,&direct));file->mapped_size=mapped_size;
 assert(!lr_qcow2_read_view(q,cs-1,2,&direct));
 BlockBackend blk={.m={.kind=LR_MEDIUM_QCOW2,.qcow2=q}};
 DMA dma={0};IDEBus bus={&dma};IDEState state={.blk=&blk,.bus=&bus};QLIST_INIT(&state.buffered_requests);
 QEMUIOVector a,b;qemu_iovec_init_buf(&a,out,cs);qemu_iovec_init_buf(&b,out+cs,cs);
 memset(out,0xcc,cs*2);int ok=0;unsigned reads=legacy_reads;
 /* Mapped data and a decoded cache view coexist without scratch storage. */
 BlockAIOCB *compressed=ide_buffered_readv(&state,cs/512,&b,cs/512,completed,&ok);
 BlockAIOCB *mapped=ide_buffered_readv(&state,0,&a,cs/512,completed,&ok);
 assert(q->view_owner->held&&file->views==1&&!scratch_live&&legacy_reads==reads);
 for(size_t i=0;i<cs*2;i++)assert(out[i]==0xcc);
 lr_qcow2_close(q);lr_file_close(file);
 finish(compressed);finish(mapped);assert(!memcmp(out,expected,cs*2));
 ide_buffered_read_cache_clear();assert(!req_live&&!scratch_live);
 free(out);free(expected);free(mapping);
 puts("Mapped QCOW2 views: pointer identity, zero/short/cross-cluster fallbacks, mixed decoded/mapped completion after close PASS");
}
static int patch_case(int argc,char **argv)
{
 assert(argc==6);size_t in=strtoull(argv[2],NULL,0),len=strtoull(argv[3],NULL,0);
 bool zero=atoi(argv[4]),failure=atoi(argv[5]);char error[256];
 LRFile *file=lr_file_open(argv[1],true,false);assert(file);
 LRQcow2 *q=lr_qcow2_open(file,true,error,sizeof(error));assert(q);
 size_t cs=q->cs;assert(in+len<=cs);
 uint8_t *expected=malloc(cs),*out=malloc(cs),*patch=malloc(len);assert(expected&&out&&patch);
 pattern(expected,cs,1);memset(patch,0x6d,len);
 LRQcow2ReadView view;assert(lr_qcow2_read_view(q,cs,cs,&view)==1);
 patch_source=q->dcache;patch_destination=q->cluster;patch_cluster_size=cs;
 memset(q->cluster,0xa5,cs);fail_patch_write=failure;
 int ret=zero?lr_qcow2_zero(q,cs+in,len):lr_qcow2_pwritev(q,cs+in,&(LRFileIOV){patch,len},1);
 fail_patch_write=false;
 assert(ret==(failure?-EIO:0));assert(!memcmp(view.data,expected,cs));
 size_t preserved=(cs>=4096&&len>=cs/2)?cs-len:cs;
 assert(patch_preserved_bytes==preserved);
 if(!failure)memset(expected+in,zero?0:0x6d,len);
 assert(!lr_qcow2_preadv(q,cs,&(LRFileIOV){out,cs},1));assert(!memcmp(out,expected,cs));
 lr_qcow2_release_read_view(view.owner);lr_qcow2_close(q);lr_file_close(file);
 free(expected);free(out);free(patch);
 printf("retained patch in=%zu len=%zu zero=%d failure=%d preserved=%zu PASS\n",in,len,zero,failure,preserved);
 return 0;
}
int main(int argc,char **argv)
{
 if(argc>2)return patch_case(argc,argv);
 assert(argc==2);char error[256];LRFile *file=lr_file_open(argv[1],true,false);assert(file);
 LRQcow2 *q=lr_qcow2_open(file,true,error,sizeof(error));if(!q){fprintf(stderr,"%s\n",error);abort();}
 BlockBackend blk={.m={.kind=LR_MEDIUM_QCOW2,.qcow2=q}};DMA dma={0};IDEBus bus={&dma};IDEState state={.blk=&blk,.bus=&bus};QLIST_INIT(&state.buffered_requests);
 size_t cs=q->cs;uint8_t *out=malloc(cs*2+32),*want=malloc(cs*2),*original=malloc(cs);assert(out&&want&&original);
 pattern(want,cs,0);pattern(want+cs,cs,1);pattern(original,cs,0);
 int ok=0;QEMUIOVector a,b;watching=true;
 /* Initial borrowed read publishes no caller bytes and needs no scratch copy. */
 memset(out,0xcc,cs*2+32);qemu_iovec_init_buf(&a,out,cs);
 BlockAIOCB *first=ide_buffered_readv(&state,0,&a,cs/512,completed,&ok);
 assert(q->view_owner->held==q->decoded&&q->decoded->refs==2&&!scratch_live&&!scratch_copy_bytes);
 assert(out[0]==0xcc&&calls==0);const uint8_t *held=q->view_owner->held->data;
 /* A second decode uses one alternate buffer, preserving the first view. */
 qemu_iovec_init_buf(&b,out+cs,cs);BlockAIOCB *second=ide_buffered_readv(&state,cs/512,&b,cs/512,completed,&ok);
 assert(legacy_reads==1&&decode_allocations==1&&scratch_copy_bytes==cs);
 assert(q->view_owner->held->data==held&&!memcmp(held,want,cs));
 assert(q->cached==1&&q->decoded!=q->view_owner->held&&out[cs]==0xcc);
 finish(second);finish(first);assert(!memcmp(out,want,cs*2)&&!q->view_owner->held);
 /* Cache-hit view retains the same decoded allocation and performs no copy. */
 DecodedBuffer *cached=q->decoded;unsigned allocs=decode_allocations;size_t copies=scratch_copy_bytes;
 memset(out,0xcc,cs);qemu_iovec_init_buf(&a,out,cs);first=ide_buffered_readv(&state,cs/512,&a,cs/512,completed,&ok);
 assert(q->decoded==cached&&decode_allocations==allocs&&scratch_copy_bytes==copies);finish(first);assert(!memcmp(out,want+cs,cs));
 /* A retained compressed cluster is immutable across partial rewrite. */
 first=ide_buffered_readv(&state,0,&a,cs/512,completed,&ok);held=q->view_owner->held->data;
 uint8_t patch[13];memset(patch,0x7a,sizeof(patch));assert(!lr_qcow2_pwritev(q,117,&(LRFileIOV){patch,sizeof(patch)},1));
 assert(!memcmp(held,original,cs));finish(first);assert(!memcmp(out,original,cs));
 memcpy(want+117,patch,sizeof(patch));assert(!lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1)&&!memcmp(out,want,cs));
 /* Keep a view while corrupting a different compressed stream. Error bytes stay private. */
 first=ide_buffered_readv(&state,cs/512,&a,cs/512,completed,&ok);assert(q->view_owner->held);
 uint64_t entry=*l2_entry(q,1),coff=entry&q->coff_mask;uint8_t saved[7];assert(!lr_file_pread(file,coff,saved,7));
 static const uint8_t broken[]={0,1,0,0xfe,0xff,0x5a,7};assert(!lr_file_pwrite(file,coff,broken,7));
 /* The held cache hit must still work; a cold decode is forced for the error path. */
 q->cached=~0ull;memset(out+cs,0xcc,cs);qemu_iovec_init_buf(&b,out+cs,cs);int ioerror=-EIO;
 second=ide_buffered_readv(&state,cs/512,&b,cs/512,completed,&ioerror);assert(second->ret==-EIO);
 finish(second);for(size_t i=0;i<cs;i++)assert(out[cs+i]==0xcc);
 assert(!memcmp(q->view_owner->held->data,want+cs,cs));assert(!lr_file_pwrite(file,coff,saved,7));finish(first);
 /* New view error queues once, with no fallback retry or output publication. */
 unsigned reads=legacy_reads;assert(!lr_file_pwrite(file,coff,broken,7));q->cached=~0ull;
 memset(out,0xcc,cs);first=ide_buffered_readv(&state,cs/512,&a,cs/512,completed,&ioerror);
 assert(first->ret==-EIO&&legacy_reads==reads&&!q->view_owner->held);finish(first);
 for(size_t i=0;i<cs;i++)assert(out[i]==0xcc);
 assert(!lr_file_pwrite(file,coff,saved,7));
 /* Allocation failure preserves the retained payload. */
 first=ide_buffered_readv(&state,cs/512,&a,cs/512,completed,&ok);
 q->cached=~0ull;fail_decode_alloc=true;int nomem=-ENOMEM;
 second=ide_buffered_readv(&state,cs/512,&b,cs/512,completed,&nomem);
 assert(second->ret==-ENOMEM&&!memcmp(q->view_owner->held->data,want+cs,cs));
 fail_decode_alloc=false;finish(second);finish(first);
 /* Eligibility and partial scattered payloads retain destination gaps. */
 LRQcow2ReadView view;
 assert(!lr_qcow2_read_view(q,0,cs,&view)); /* rewritten, uncompressed */
 assert(!lr_qcow2_read_view(q,cs-1,2,&view));
 assert(!lr_qcow2_read_view(q,0,0,&view));
 assert(lr_qcow2_read_view(q,-1,1,&view)==-EIO);
 assert(lr_qcow2_read_view(q,cs*2,1,&view)==-EIO);
 if(cs>=4096){
  struct iovec pieces[3]={{out,173},{out+200,0},{out+200,339}};
  QEMUIOVector scatter={.iov=pieces,.niov=3,.size=512};
  memset(out,0xcc,cs);
  first=ide_buffered_readv(&state,cs/512+1,&scatter,1,completed,&ok);
  assert(q->view_owner->held);finish(first);
  assert(!memcmp(out,want+cs+512,173));
  assert(!memcmp(out+200,want+cs+512+173,339));
  for(unsigned i=173;i<200;i++)assert(out[i]==0xcc);
 }
 /* A successful deferred payload also survives closing the image. */
 first=ide_buffered_readv(&state,cs/512,&a,cs/512,completed,&ok);
 lr_qcow2_close(q);lr_file_close(file);blk.m.qcow2=NULL;
 finish(first);assert(!memcmp(out,want+cs,cs));
 file=lr_file_open(argv[1],true,false);assert(file);
 q=lr_qcow2_open(file,true,error,sizeof(error));assert(q);blk.m.qcow2=q;
 /* Cancel, then close the image before releasing its retained view. */
 memset(out,0xcc,cs);int canceled=-ECANCELED;
 first=ide_buffered_readv(&state,cs/512,&a,cs/512,completed,&canceled);
 assert(q->view_owner->held);unsigned before=calls;
 ide_cancel_dma_sync(&state);assert(calls==before+1);
 lr_qcow2_close(q);lr_file_close(file);blk.m.qcow2=NULL;
 finish(first);assert(calls==before+1);
 for(size_t i=0;i<cs;i++)assert(out[i]==0xcc);
 ide_buffered_read_cache_clear();assert(!req_live&&!scratch_live);
 for(unsigned i=0;i<64;i++)assert(!pending[i].live);
 assert(queued==releases);free(out);free(want);free(original);
 mapped_cases(argv[1]);mapped_qcow_cases(argv[1]);assert(queued==releases);
 puts("QCOW2 retained IDE views: copies, overlap, writes, errors, allocation failure and canceled-close lifetime passed");
 return 0;
}

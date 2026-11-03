/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Deterministic copy/lifetime checks, not a throughput benchmark. */
#include "block/libretro-file.c"
#include "block/libretro-qcow2.h"
#include <encodings/deflate.h>
#include <assert.h>
#include <stdio.h>
static bool watching,force_fallback,fail_input;
static LRQcow2 *tracked;
static const uint8_t *observed_input;
static unsigned input_reads,view_queries;
static int observed_read(LRFile *,int64_t,void *,size_t);
static const uint8_t *observed_view(LRFile *,int64_t,size_t);
static void observed_set_in(void *,const uint8_t *,size_t);
#define lr_file_pread observed_read
#define lr_file_view observed_view
#define rinflate_set_in observed_set_in
#ifndef QCOW_INPUT_SOURCE
#define QCOW_INPUT_SOURCE "block/libretro-qcow2.c"
#endif
#include QCOW_INPUT_SOURCE
#undef lr_file_pread
#undef lr_file_view
#undef rinflate_set_in
static int observed_read(LRFile*f,int64_t off,void*buf,size_t n){
 if(watching&&buf==tracked->cbuf){input_reads++;if(fail_input)return -EIO;}
 return lr_file_pread(f,off,buf,n);
}
static const uint8_t *observed_view(LRFile*f,int64_t off,size_t n){
 if(watching)view_queries++;
 return force_fallback?NULL:lr_file_view(f,off,n);
}
static void observed_set_in(void*s,const uint8_t*in,size_t n){observed_input=in;rinflate_set_in(s,in,n);}
static void pattern(uint8_t*out,size_t cs,unsigned gc){for(size_t i=0;i<cs;i++)out[i]=(uint8_t)(i/17+i%13+gc*37);}
static void run(const char*path,unsigned mode){
 LRFile*native=mode==4?lr_file_open(path,false,false):lr_file_open_native(path,mode==2,false);assert(native);int64_t size=lr_file_size(native);assert(size>0);
 uint8_t*image=malloc((size_t)size);assert(image);assert(!lr_file_pread(native,0,image,(size_t)size));
 LRFile mapped={.mapped=image,.mapped_size=size};LRFile*f=mode>=2?native:&mapped;
 char error[256];LRQcow2*q=lr_qcow2_open(f,mode==2,error,sizeof(error));if(!q){fprintf(stderr,"%s\n",error);abort();}
 bool direct=mode==0||mode==4;
 if(mode==4)assert(lr_file_view(native,0,1));
 tracked=q;size_t cs=(size_t)q->cs;uint8_t*out=malloc(cs),*expected=malloc(cs);assert(out&&expected);
 force_fallback=mode==1;fail_input=false;watching=true;
 for(unsigned step=0;step<4;step++){
  unsigned gc=step&1;assert(*l2_entry(q,gc)&F_COMPRESSED);uint64_t coff=*l2_entry(q,gc)&q->coff_mask;
  memset(q->cbuf,0xa5,cs*2+512);input_reads=view_queries=0;observed_input=NULL;
  LRFileIOV iov[3]={{out,cs/2},{NULL,0},{out+cs/2,cs-cs/2}};
  assert(!lr_qcow2_preadv(q,(int64_t)gc*cs,iov,3));pattern(expected,cs,gc);assert(!memcmp(out,expected,cs));
  assert(q->cached==gc&&input_reads==(direct?0u:1u)&&view_queries==(mode==2?0u:1u));
  assert(observed_input==(mode==0?image+coff:mode==4?lr_file_view(native,(int64_t)coff,1):q->cbuf));
  if(direct)for(size_t i=0;i<cs*2+512;i++)assert(q->cbuf[i]==0xa5);
  unsigned reads=input_reads,views=view_queries;assert(!lr_qcow2_preadv(q,(int64_t)gc*cs+17,&(LRFileIOV){out,73},1));assert(!memcmp(out,expected+17,73));
  assert(reads==input_reads&&views==view_queries);
 }
 if(mode<2){
  uint64_t coff=*l2_entry(q,0)&q->coff_mask;uint8_t saved[7];
  /* A complete stored byte followed by reserved BTYPE=3: decoding writes
   * cache data before failing. No partial output may reach the caller. */
  static const uint8_t broken[]={0,1,0,0xfe,0xff,0x5a,7};
  assert(coff+sizeof(broken)<=(uint64_t)size);memcpy(saved,image+coff,sizeof(saved));
  memcpy(image+coff,broken,sizeof(broken));assert(q->cached==1);
  memset(q->dcache,0xa5,cs);memset(out,0xcc,cs);
  assert(lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1)==-EIO&&q->cached==~0ull);
  assert(q->dcache[0]==0x5a);
  for(size_t i=0;i<cs;i++)assert(out[i]==0xcc);
  memcpy(image+coff,saved,sizeof(saved));assert(!lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1));pattern(expected,cs,0);assert(!memcmp(out,expected,cs));
  q->cached=~0ull;force_fallback=true;fail_input=true;memset(out,0xcc,cs);
  assert(lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1)==-EIO);for(size_t i=0;i<cs;i++)assert(out[i]==0xcc);
  fail_input=false;force_fallback=false;mapped.mapped_size=(int64_t)coff;input_reads=view_queries=0;
  assert(lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1)==-EIO);assert(view_queries==1&&input_reads==1);
  for(size_t i=0;i<cs;i++)assert(out[i]==0xcc);
  mapped.mapped_size=size;input_reads=view_queries=0;
  assert(!lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1));pattern(expected,cs,0);assert(!memcmp(out,expected,cs));
  assert(!input_reads&&view_queries==1&&observed_input==image+coff);
  /* Exact-end views are valid. A one-byte-short view must use the same
   * zero-padding fallback as a deliberately unavailable view. The omitted
   * byte may be sector padding, so do not assume decompression must fail. */
  uint64_t entry=*l2_entry(q,0);
  size_t encoded=(size_t)((((entry>>q->csize_shift)&q->csize_mask)+1)*512-(coff&511));
  assert(coff+encoded<=(uint64_t)size&&encoded>0);
  mapped.mapped_size=(int64_t)(coff+encoded);q->cached=~0ull;input_reads=view_queries=0;
  assert(!lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1));assert(!memcmp(out,expected,cs));
  assert(!input_reads&&view_queries==1&&observed_input==image+coff);
  mapped.mapped_size--;q->cached=~0ull;memset(out,0xcc,cs);input_reads=view_queries=0;
  int short_view=lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1);
  assert(input_reads==1&&view_queries==1);memcpy(expected,out,cs);
  force_fallback=true;q->cached=~0ull;memset(out,0xcc,cs);input_reads=view_queries=0;
  int unavailable=lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1);
  assert(short_view==unavailable&&!memcmp(out,expected,cs)&&input_reads==1&&view_queries==1);
  mapped.mapped_size=size;force_fallback=false;q->cached=~0ull;
  assert(!lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1));pattern(expected,cs,0);assert(!memcmp(out,expected,cs));
 }
 watching=false;lr_qcow2_close(q);lr_file_close(native);free(image);free(out);free(expected);
 printf("compressed input mode=%u: pointer/copy counts, cache reuse and error publication PASS\n",mode);
}
int main(int argc,char**argv){
 assert(argc==2);
 for(unsigned repeat=0;repeat<2;repeat++){
  for(unsigned mode=0;mode<4;mode++)run(argv[1],mode);
#if LR_FILE_FILESTREAM
  run(argv[1],4); /* Require actual libretro-common mapping, not a mock. */
#endif
 }
 return 0;
}

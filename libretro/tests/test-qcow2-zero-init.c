/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "block/libretro-qcow2.h"
static LRQcow2 *tracked;
static uint8_t *wanted;
static bool watching;
static int fail_data;
static size_t zeroed_bytes;
static unsigned data_writes;
static void *observed_zero(void *,int,size_t);
static int observed_write(LRFile *,int64_t,const void *,size_t);
#define memset observed_zero
#define lr_file_pwrite observed_write
/* PRODUCTION */
#undef memset
#undef lr_file_pwrite
static void *observed_zero(void*dst,int value,size_t n){
 if(watching&&value==0){uintptr_t a=(uintptr_t)dst,b=(uintptr_t)tracked->cluster;
  if(a>=b&&a-b<=tracked->cs&&n<=tracked->cs-(a-b))zeroed_bytes+=n;
 }
 return memset(dst,value,n);
}
static int observed_write(LRFile*f,int64_t off,const void*buf,size_t n){
 if(watching&&buf==tracked->cluster&&n==tracked->cs){
  data_writes++;assert(!memcmp(buf,wanted,n));if(fail_data){if(fail_data==2)assert(!lr_file_pwrite(f,off,buf,n/2));return -EIO;}
 }
 return lr_file_pwrite(f,off,buf,n);
}
int main(int argc,char**argv){
 assert(argc==3);fail_data=atoi(argv[2]);char error[256];
 LRFile*f=lr_file_open(argv[1],true,false);assert(f);LRQcow2*q=lr_qcow2_open(f,true,error,sizeof(error));if(!q){fprintf(stderr,"%s\n",error);abort();}tracked=q;
 size_t cs=(size_t)q->cs;uint8_t*input=malloc(cs),*out=malloc(cs);wanted=malloc(cs);assert(input&&out&&wanted);
 memset(input,0x5e,cs);
 const size_t cases[][2]={{0,0},{0,1},{cs-1,1},{cs/2,1},{1,cs-2},{0,cs-1},{1,cs-1},{cs/4,cs/2}};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  size_t in=cases[i][0],n=cases[i][1];assert(!lr_qcow2_zero(q,0,cs));
  memset(wanted,0,cs);memset(wanted+in,0x5e,n);memset(q->cluster,0xa5,cs);
  LRFileIOV pieces[3]={{input,n/2},{NULL,0},{input+n/2,n-n/2}};
  zeroed_bytes=data_writes=0;watching=true;int ret=lr_qcow2_pwritev(q,(int64_t)in,pieces,3);watching=false;
  assert(ret==(fail_data&&n?-EIO:0));assert(data_writes==(n?1u:0u));
#ifdef QCOW_ZERO_EXPECT_FULL
  assert(zeroed_bytes==(n?cs:0));
#else
  assert(zeroed_bytes==(n?(cs>=4096&&n>=cs/2?cs-n:cs):0));
#endif
  if(fail_data)memset(wanted,0,cs);
  memset(out,0xcc,cs);assert(!lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1));assert(!memcmp(out,wanted,cs));
 }
 assert(!lr_qcow2_flush(q));lr_qcow2_close(q);lr_file_close(f);
 /* Reopen verifies that metadata and on-disk untouched bytes agree. */
 f=lr_file_open(argv[1],false,false);assert(f);q=lr_qcow2_open(f,false,error,sizeof(error));assert(q);
 assert(!lr_qcow2_preadv(q,0,&(LRFileIOV){out,cs},1)&&!memcmp(out,wanted,cs));
 lr_qcow2_close(q);lr_file_close(f);free(input);free(out);free(wanted);
 printf("QCOW2 zero-init: cluster=%zu failure=%d, 8 empty/edge/middle/gapped writes, poison guards, zero-byte counts and reopen PASS\n",cs,fail_data);return 0;
}

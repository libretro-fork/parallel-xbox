/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "block/libretro-qcow2.h"
#ifndef TEST_CLUSTER_SIZE
#define TEST_CLUSTER_SIZE 65536
#endif
/* Count only production workspace mallocs, and inject each failure. */
static void *allocations[8];
static unsigned allocation_calls, allocation_live, allocation_fail;
static void *counted_malloc(size_t n)
{
    allocation_calls++;
    if (allocation_fail == allocation_calls) { return NULL; }
    void *p = malloc(n);
    if (p) {
        unsigned i;
        for (i = 0; i < 8 && allocations[i]; i++) {}
        assert(i < 8);
        allocations[i] = p;
        allocation_live++;
    }
    return p;
}
static void counted_free(void *p)
{
    for (unsigned i = 0; p && i < 8; i++) {
        if (allocations[i] == p) {
            allocations[i] = NULL;
            allocation_live--;
            break;
        }
    }
    free(p);
}
static bool watching,fail_data;
static unsigned full_copies,data_writes;
static LRQcow2 *tracked;
static void *counted_copy(void *dst,const void *src,size_t n){if(watching&&n==TEST_CLUSTER_SIZE)full_copies++;return memcpy(dst,src,n);}
static int counted_write(LRFile *,int64_t,const void *,size_t);
#define malloc counted_malloc
#define free counted_free
#define memcpy counted_copy
#define lr_file_pwrite counted_write
/* PRODUCTION */
#undef malloc
#undef free
#undef memcpy
#undef lr_file_pwrite
static int counted_write(LRFile *f,int64_t off,const void *buf,size_t n){
 if(watching&&n==tracked->cs&&(buf==tracked->cluster||buf==tracked->dcache)){
  data_writes++;
#ifdef WORKSPACE_BASELINE
  assert(buf==tracked->cluster);
#else
  assert(buf==tracked->dcache);
#endif
  if(fail_data)return -EIO;
 }
 return lr_file_pwrite(f,off,buf,n);
}
static void expected(unsigned char *dst,unsigned gc){for(unsigned i=0;i<TEST_CLUSTER_SIZE;i++)dst[i]=(unsigned char)(i/17+i%13+gc*37);}
int main(int argc,char **argv){
 assert(argc==3);bool failure=atoi(argv[2])!=0;char error[256];
 LRFile *reader=lr_file_open(argv[1],false,false);assert(reader);
 unsigned char *robytes=malloc(TEST_CLUSTER_SIZE),*roexpected=malloc(TEST_CLUSTER_SIZE);
 assert(robytes&&roexpected);
 allocation_calls=0;
 LRQcow2 *ro=lr_qcow2_open(reader,false,error,sizeof(error));assert(ro);
#ifdef WORKSPACE_BASELINE
 assert(ro->cluster && ro->table && allocation_calls==4 && allocation_live==4);
 const unsigned readonly_allocations=4;
#else
 assert(!ro->cluster && !ro->table && allocation_calls==3 && allocation_live==2);
 const unsigned readonly_allocations=3;
#endif
 LRFileIOV rovec={robytes,TEST_CLUSTER_SIZE};
 for(unsigned gc=0;gc<2;gc++){
  expected(roexpected,gc);
  assert(!lr_qcow2_preadv(ro,gc*TEST_CLUSTER_SIZE,&rovec,1));
  assert(!memcmp(robytes,roexpected,TEST_CLUSTER_SIZE));
 }
 assert(allocation_calls==readonly_allocations);
 assert(lr_qcow2_pwritev(ro,0,&rovec,1)==-EPERM);
 assert(lr_qcow2_zero(ro,0,1)==-EPERM);
 assert(!lr_qcow2_flush(ro));
 lr_qcow2_close(ro);assert(!allocation_live);
 for(unsigned fail=1;fail<=readonly_allocations;fail++){
  allocation_calls=0;allocation_fail=fail;
  ro=lr_qcow2_open(reader,false,error,sizeof(error));
  assert(!ro&&!allocation_live);
 }
 allocation_fail=allocation_calls=0;
 lr_file_close(reader);free(robytes);free(roexpected);
 printf("QCOW2 readonly: %u workspace allocations; all allocation failures release buffers PASS\n",readonly_allocations);
 LRFile *file=lr_file_open(argv[1],true,false);assert(file);LRQcow2 *q=lr_qcow2_open(file,true,error,sizeof(error));
 assert(q&&q->cluster&&q->table&&allocation_calls==4&&allocation_live==4);
 if(!q){fprintf(stderr,"open: %s\n",error);abort();}tracked=q;assert(q->cs==TEST_CLUSTER_SIZE);
 unsigned char *original=malloc(TEST_CLUSTER_SIZE),*readback=malloc(TEST_CLUSTER_SIZE);assert(original&&readback);
 unsigned char patch[73];memset(patch,0x5e,sizeof(patch));
 for(unsigned gc=0;gc<2;gc++){
  assert(*l2_entry(q,gc)&F_COMPRESSED);expected(original,gc);
  LRFileIOV output={readback,TEST_CLUSTER_SIZE};assert(!lr_qcow2_preadv(q,gc*TEST_CLUSTER_SIZE,&output,1));assert(!memcmp(original,readback,TEST_CLUSTER_SIZE));assert(q->cached==gc);
  memset(q->cluster,0xa5,TEST_CLUSTER_SIZE);full_copies=data_writes=0;fail_data=failure;watching=true;
  LRFileIOV input={patch,sizeof(patch)};
  int ret=gc?lr_qcow2_zero(q,TEST_CLUSTER_SIZE+211,91):lr_qcow2_pwritev(q,111,&input,1);
  watching=false;fail_data=false;assert(data_writes==1);
#ifdef WORKSPACE_BASELINE
  assert(full_copies==1);
#else
  assert(!full_copies);for(unsigned i=0;i<TEST_CLUSTER_SIZE;i++)assert(q->cluster[i]==0xa5);
#endif
  assert(q->cached==~0ull);
  if(failure){assert(ret==-EIO);}else{assert(!ret);if(gc)memset(original+211,0,91);else memcpy(original+111,patch,sizeof(patch));}
  assert(!lr_qcow2_preadv(q,gc*TEST_CLUSTER_SIZE,&output,1));assert(!memcmp(original,readback,TEST_CLUSTER_SIZE));
 }
 lr_qcow2_close(q);assert(!allocation_live);lr_file_close(file);free(original);free(readback);
 printf("QCOW2 compressed partial write/zero: full-cluster copies=%u per operation; %s data, cache invalidation and neighbors PASS\n",full_copies,failure?"failed-write original":"successful-write");return 0;
}
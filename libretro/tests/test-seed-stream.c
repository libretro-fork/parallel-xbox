/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qemu/file-map.h"
#define SEED_MAGIC 0x44455358u
#define SEED_VERSION 1u
#define SEED_MAX_ITEMS 65536u
#define g_free free
static unsigned char image[65536],before[65536],expected[65536];
static size_t length;
static bool path_ok=true,map_ok=true,active,fail_open,fail_close,fail_rename,short_hit,temp_present;
static unsigned opens,commits,cleanups,writes,fail_write,maps,unmaps,direct_writes;
static unsigned char *mapped;
static char *seed_path(void){return path_ok?strdup("private.seed"):NULL;}
bool xemu_file_map(XemuFileMap *m,const char *path){assert(path&&!active);if(!map_ok)return false;mapped=malloc(length?length:1);assert(mapped);memcpy(mapped,image,length);*m=(XemuFileMap){mapped,length,mapped,NULL};active=true;maps++;return true;}
void xemu_file_unmap(XemuFileMap *m){assert(active);memset(mapped,0xa5,length);free(mapped);mapped=NULL;active=false;unmaps++;*m=(XemuFileMap){0};}
typedef struct {unsigned char bytes[65536];size_t size;} RFILE;
static RFILE stream;
static RFILE *filestream_open_atomic(const char *path){assert(path);opens++;if(fail_open)return NULL;stream.size=0;temp_present=true;return &stream;}
static int64_t filestream_write(RFILE *f,const void *p,int64_t n){assert(f==&stream&&n>0&&(uint64_t)n<=sizeof(f->bytes)-f->size);writes++;uintptr_t addr=(uintptr_t)p,base=(uintptr_t)mapped;if(active&&addr>=base&&addr-base<length){assert((uint64_t)n<=length-(addr-base));direct_writes++;}if(writes==fail_write){short_hit=true;return n-1;}memcpy(f->bytes+f->size,p,n);f->size+=n;return n;}
static int filestream_commit_atomic(RFILE *f,const char *path,bool ok){assert(f==&stream&&path&&!active);commits++;if(!ok||fail_close){temp_present=false;return -1;}if(fail_rename)return 1;memcpy(image,f->bytes,f->size);length=f->size;temp_present=false;return 0;}
static int filestream_finish_atomic(const char *path,bool ok){assert(path&&!ok&&!active);temp_present=false;cleanups++;return -1;}
/* PRODUCTION */
static unsigned rng=42;
static unsigned random_u(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
/* Independent bounded byte-image rewrite, preserving valid prefixes. */
static size_t oracle(const void *items,unsigned count,unsigned *kept){
 size_t out=sizeof(SeedHeader),off=sizeof(SeedHeader);*kept=0;SeedHeader h;
 if(map_ok&&length>=sizeof(h)){
  memcpy(&h,image,sizeof(h));
  if(h.magic==SEED_MAGIC&&h.version==SEED_VERSION)for(unsigned i=0;i<h.nsections;i++){
   if(length-off<sizeof(SeedSection))break;
   SeedSection sec;memcpy(&sec,image+off,sizeof(sec));uint64_t bytes=(uint64_t)sec.itemsize*sec.count;
   if(sec.count>SEED_MAX_ITEMS||bytes>length-off-sizeof(sec))break;
   size_t span=sizeof(sec)+(size_t)bytes;if(sec.id!=7){memcpy(expected+out,image+off,span);out+=span;(*kept)++;}off+=span;
  }
 }
 h=(SeedHeader){SEED_MAGIC,SEED_VERSION,*kept+1,0};memcpy(expected,&h,sizeof(h));SeedSection sec={7,4,count,0};memcpy(expected+out,&sec,sizeof(sec));out+=sizeof(sec);memcpy(expected+out,items,count*4);return out+count*4;
}
static void reset(void){assert(!active);opens=commits=cleanups=writes=maps=unmaps=direct_writes=0;short_hit=temp_present=false;fail_open=fail_close=fail_rename=false;fail_write=0;path_ok=map_ok=true;}
int main(void){
 unsigned cases=0;uint32_t payload[]={1,2,3,4,5,6,7};
 for(unsigned trial=0;trial<256;trial++){
  SeedHeader h={SEED_MAGIC,SEED_VERSION,1+random_u()%7,random_u()};memcpy(before,&h,sizeof(h));size_t size=sizeof(h);
  for(unsigned i=0;i<h.nsections;i++){SeedSection sec={i%3==0?7:100+i,1+random_u()%16,random_u()%33,random_u()};memcpy(before+size,&sec,sizeof(sec));size+=sizeof(sec);for(unsigned j=0;j<sec.itemsize*sec.count;j++)before[size++]=(unsigned char)random_u();}
  if(trial%4==0)size=random_u()%(size+1);
  if(trial%9==0 && size>=sizeof(h))before[0]^=1;
  if(trial%13==0 && size>=sizeof(h)+sizeof(SeedSection)){SeedSection bad={100,UINT32_MAX,65536,0};memcpy(before+sizeof(h),&bad,sizeof(bad));}
  for(unsigned failure=0;failure<10;failure++){
   reset();memcpy(image,before,size);length=size;map_ok=trial%7!=0;
   fail_open=failure==1;fail_close=failure==8;fail_rename=failure==9;if(failure>=2&&failure<=7)fail_write=failure-1;
   unsigned kept;size_t wanted=oracle(payload,7,&kept);
   bool ok=pgraph_seed_write(7,4,payload,7),should=!fail_open&&!fail_close&&!fail_rename&&!short_hit;
   assert(ok==should&&!active&&!temp_present&&maps==unmaps);
   assert(opens==1&&commits==!fail_open&&cleanups==(!fail_open&&!short_hit&&!fail_close&&fail_rename));
   if(ok){assert(length==wanted&&!memcmp(image,expected,wanted));assert(direct_writes==kept);}else assert(length==size&&!memcmp(image,before,size));cases++;
  }
 }
 reset();path_ok=false;assert(!pgraph_seed_write(7,4,payload,7)&&!opens&&!maps);
 reset();assert(!pgraph_seed_write(7,4,payload,0)&&!opens&&!maps);assert(!pgraph_seed_write(7,4,payload,SEED_MAX_ITEMS+1)&&!opens&&!maps);
 assert(!pgraph_seed_write(7,SIZE_MAX,payload,2)&&!opens&&!maps);
 assert(seed_stream_write(NULL,NULL,0));assert(!seed_stream_write(NULL,NULL,UINT64_MAX)&&!writes);
 printf("Seed stream: %u byte-image/failure cases; mapped spans, duplicate/unknown sections, malformed prefixes, overflow, close/rename cleanup and unmap-before-commit PASS\n",cases);
}

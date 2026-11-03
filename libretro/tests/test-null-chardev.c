/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <limits.h>
typedef struct Chardev Chardev;
typedef struct {int (*chr_write)(Chardev*,const uint8_t*,int);} ChardevClass;
struct Chardev {ChardevClass *klass;int logfd,chr_write_lock;};
#define CHARDEV_GET_CLASS(s) ((s)->klass)
static unsigned locks,unlocks,sleeps,backend_calls,log_calls;static bool held;
static unsigned backend_mode,log_mode;static uint8_t log_bytes[2048];static size_t log_length;
static void qemu_mutex_lock(int *mutex){(void)mutex;assert(!held);held=true;locks++;}
static void qemu_mutex_unlock(int *mutex){(void)mutex;assert(held);held=false;unlocks++;}
static void sthread_yield(void){assert(held);sleeps++;}
static ssize_t fake_write(int fd,const void *data,size_t size){assert(held&&fd==7);log_calls++;if(log_mode==1&&log_calls==1){errno=EAGAIN;return -1;}if(log_mode==2){errno=EIO;return -1;}size_t n=size<7?size:7;assert(log_length+n<=sizeof(log_bytes));memcpy(log_bytes+log_length,data,n);log_length+=n;return n;}
#define write fake_write
/* PRODUCTION */
#undef write
static int backend(Chardev *chr,const uint8_t *data,int len)
{(void)chr;assert(held&&data&&len>0);backend_calls++;if(backend_mode==2&&backend_calls==1){errno=EAGAIN;return -1;}if(backend_mode==3||(backend_mode==4&&backend_calls==2)){errno=EIO;return -1;}return (backend_mode==1||backend_mode==4)&&len>2?2:len;}
int main(void)
{
 ChardevClass sink={null_chr_write},real={backend};Chardev chr={&sink,-1,0};uint8_t bytes[1024],copy[1024];for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(uint8_t)(i*37+19);memcpy(copy,bytes,sizeof(bytes));unsigned lengths[]={0,1,2,3,7,16,255,1024};unsigned cases=0;
 for(unsigned null=0;null<2;null++)for(unsigned logged=0;logged<2;logged++)for(unsigned all=0;all<2;all++)for(unsigned mode=0;mode<5;mode++)for(unsigned lm=0;lm<3;lm++)for(unsigned li=0;li<sizeof(lengths)/sizeof(lengths[0]);li++) {
  unsigned len=lengths[li];chr.klass=null?&sink:&real;chr.logfd=logged?7:-1;locks=unlocks=sleeps=backend_calls=log_calls=0;log_length=0;backend_mode=mode;log_mode=lm;
  int wanted=(int)len;unsigned logged_bytes=len;
  if(!null&&len){if(mode==3||(mode==2&&!all))wanted=-1;else if(mode==4&&len>2){if(all)wanted=-1;else wanted=2;logged_bytes=2;}else if(mode==1&&!all){wanted=len<2?len:2;logged_bytes=wanted;}}
  int got=qemu_chr_write(&chr,bytes,len,all);assert(got==wanted&&!held&&!memcmp(bytes,copy,sizeof(bytes)));
  bool fast=null&&!logged;assert(locks==!fast&&unlocks==!fast);if(fast)assert(!backend_calls&&!log_calls&&!sleeps);
  if(!logged||!len||lm==2)assert(!log_length);else assert(log_length==logged_bytes&&!memcmp(log_bytes,bytes,logged_bytes));
  if(!null&&len&&mode==2)assert(sleeps==(all?1u:0u)+(logged&&lm==1?1u:0u));
  cases++;
 }
 /* The sink must not dereference the source or overflow offsets. */
 chr=(Chardev){&sink,-1,0};locks=unlocks=0;int offset=-123;
 assert(qemu_chr_write_buffer(&chr,NULL,INT_MAX,&offset,true)==INT_MAX&&offset==INT_MAX);
 assert(qemu_chr_write(&chr,NULL,-7,false)==0);assert(qemu_chr_write(&chr,NULL,0,true)==0);assert(!locks&&!unlocks);
 printf("Null chardev: %u write/log/retry/error schedules; no sink locks or payload reads, preserved fallback offsets/logging and zero/negative/INT_MAX lengths PASS\n",cases);return 0;
}

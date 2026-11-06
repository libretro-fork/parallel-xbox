/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include "block/libretro-chd.h"
#include "block/libretro-read-view.h"

typedef struct Source {
    uint8_t *bytes; size_t length; uintptr_t owner;
    bool live, gate, mapped; unsigned closes, worker_reads; retro_atomic_int_t notifications;
    retro_atomic_int_t entered, permit, fail;
    retro_eventcount_t wake;
} Source;
static void await(retro_atomic_int_t *condition, retro_eventcount_t *wake)
{
    while (!retro_atomic_load_acquire_int(condition)) {
        int key=retro_eventcount_prepare_wait(wake);
        if (retro_atomic_load_acquire_int(condition)) retro_eventcount_cancel_wait(wake);
        else retro_eventcount_commit_wait(wake,key);
    }
}
static void enter(Source*s)
{
    if(!s->live)return;
    assert(sthread_get_current_thread_id()!=s->owner);
    s->worker_reads++;
    if(s->gate) {
        retro_atomic_store_release_int(&s->entered,1);
        retro_eventcount_notify(&s->wake);
        await(&s->permit,&s->wake);
    }
}
static int read_source(void*arg,uint64_t off,void*dst,size_t n)
{
    Source*s=arg;enter(s);
    if(retro_atomic_load_acquire_int(&s->fail))return -EIO;
    assert(off<=s->length && n<=s->length-off);
    memcpy(dst,s->bytes+off,n);return 0;
}
static const uint8_t *view_source(void*arg,uint64_t off,size_t n)
{
    Source*s=arg;
    if(!s->mapped)return NULL;
    enter(s);assert(off<=s->length && n<=s->length-off);
    return s->bytes+off;
}
static void notify_source(void*arg){Source*s=arg;retro_atomic_fetch_add_int(&s->notifications,1);}
static void close_source(void*arg)
{
    Source*s=arg;assert(sthread_get_current_thread_id()==s->owner);
    s->closes++;free(s->bytes);s->bytes=NULL;
}
static uint8_t *load(const char*dir,const char*name,size_t*n)
{
    char path[4096];snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE*f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long z=ftell(f);assert(z>=0);rewind(f);*n=(size_t)z;
    uint8_t*p=malloc(*n+1);assert(p);assert(fread(p,1,*n,f)==*n);fclose(f);return p;
}
static int wait_result(LRCHDRequest*r,const void**bytes,size_t*n)
{
    int e;
    /* Test-driver scheduling only; production poll has no wait. */
    while(!(e=lr_chd_poll(r,bytes,n))) {
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec ts={0,1000000};nanosleep(&ts,NULL);
#endif
    }
    if(e==1)assert(!((uintptr_t)*bytes & 63));
    return e;
}
static void run(const char*dir,const char*name,bool mapped)
{
    size_t raw_len;uint8_t*raw=load(dir,"raw.bin",&raw_len);
    Source s={.owner=sthread_get_current_thread_id(),.mapped=mapped,.gate=true};
    s.bytes=load(dir,name,&s.length);
    retro_atomic_int_init(&s.entered,0);retro_atomic_int_init(&s.permit,0);
    retro_atomic_int_init(&s.fail,0);assert(retro_eventcount_init_lock_free(&s.wake));
    retro_atomic_int_init(&s.notifications,0);
    LRCHDSource src={&s,s.length,read_source,view_source,close_source,notify_source,&s};int error;
    LRCHD*c=lr_chd_open(&src,&error);assert(c && !error && lr_chd_size(c)==raw_len);
    uint8_t metadata[713];
    assert(!lr_chd_read_metadata(c,129,metadata,sizeof(metadata)));
    assert(!memcmp(metadata,raw+129,sizeof(metadata)));
    LRCHDRequest*r[LR_CHD_SLOTS],*extra; s.live=true;
    for(unsigned i=0;i<LR_CHD_SLOTS;i++)assert(!lr_chd_submit(c,4096+i*257,4000,&r[i]));
    await(&s.entered,&s.wake);
    const void*bytes;size_t n;
    for(unsigned j=0;j<50000;j++)for(unsigned i=0;i<LR_CHD_SLOTS;i++)
        assert(!lr_chd_poll(r[i],&bytes,&n) && !bytes && !n);
    assert(lr_chd_submit(c,0,1,&extra)==-EAGAIN && !extra);
    lr_chd_release(r[0]); /* Abandon a running decode; never reuse early. */
    assert(lr_chd_submit(c,0,1,&extra)==-EAGAIN);
    retro_atomic_store_release_int(&s.permit,1);retro_eventcount_notify(&s.wake);
    for(unsigned i=1;i<LR_CHD_SLOTS;i++){
        int got=wait_result(r[i],&bytes,&n);
        if(got!=1 || n!=4000) fprintf(stderr,"%s mapped=%d slot=%u ret=%d len=%zu\n",name,mapped,i,got,n);
        assert(got==1 && n==4000);
        assert(!memcmp(bytes,raw+4096+i*257,n));
    }
    /* The first slot may be reclaimed, all other completed views stay live. */
    assert(!lr_chd_submit(c,raw_len-2048,2048,&extra));
    assert(wait_result(extra,&bytes,&n)==1 && n==2048);
    assert(!memcmp(bytes,raw+raw_len-2048,n));lr_chd_release(extra);
    for(unsigned i=2;i<LR_CHD_SLOTS;i++)lr_chd_release(r[i]);
    for(unsigned i=0;i<500;i++){
        size_t off=(i*977u)%(raw_len-6501);
        assert(!lr_chd_submit(c,off,6501,&extra));
        assert(wait_result(extra,&bytes,&n)==1 && n==6501);
        assert(!memcmp(bytes,raw+off,n));lr_chd_release(extra);
    }
    assert(lr_chd_submit(c,UINT64_MAX,1,&extra)==-EINVAL);
    assert(lr_chd_submit(c,raw_len,1,&extra)==-EINVAL);
    assert(lr_chd_submit(c,0,LR_CHD_MAX_READ+1,&extra)==-EINVAL);
    /* Force a fresh hunk for the provider-error lane. */
    if(!mapped){
        retro_atomic_store_release_int(&s.fail,1);
        assert(!lr_chd_submit(c,4096,8192,&extra));
        assert(wait_result(extra,&bytes,&n)==-EIO && !bytes && !n);
        lr_chd_release(extra);retro_atomic_store_release_int(&s.fail,0);
    }
    assert(!lr_chd_submit(c,4096,8192,&extra));
    assert(wait_result(extra,&bytes,&n)==1 && !memcmp(bytes,raw+4096,n));
    const void*held=bytes;
    const void*old;size_t old_n;
    assert(lr_chd_poll(r[1],&old,&old_n)==1 && old_n==4000);
    assert(!memcmp(old,raw+4096+257,old_n));
    /* A queued request and a completed view both survive medium teardown. */
    assert(!lr_chd_submit(c,8192,8192,&r[0]));
    lr_chd_close(c);assert(s.closes==1 && !s.bytes);
    assert(retro_atomic_load_acquire_int(&s.notifications)>0);
    assert(!memcmp(held,raw+4096,8192));
    assert(lr_chd_poll(extra,&bytes,&n)==1 && bytes==held);
    assert(!memcmp(old,raw+4096+257,old_n));lr_chd_release(r[1]);
    ((LRReadViewOwner*)extra)->release(extra);
    int e=lr_chd_poll(r[0],&bytes,&n);assert(e==1 || e==-ECANCELED);
    if(e==1)assert(!memcmp(bytes,raw+8192,n));
    lr_chd_release(r[0]);
    assert(s.worker_reads>0);retro_eventcount_free(&s.wake);free(raw);
}
static void invalid(const char*dir)
{
    for(unsigned mode=0;mode<4;mode++){
        Source s={.owner=sthread_get_current_thread_id()};
        s.bytes=load(dir,"v5.chd",&s.length);
        if(mode==0)s.bytes[0]='Z';
        if(mode==1){s.bytes[60]=0;s.bytes[61]=0;s.bytes[62]=2;s.bytes[63]=0;}
        if(mode==2)s.bytes[104]=1; /* external parent SHA-1 */
        LRCHDSource src={&s,mode==3?8:s.length,read_source,NULL,close_source,NULL,NULL};
        int e=0;assert(!lr_chd_open(&src,&e) && e<0 && !s.closes);
        free(s.bytes);
    }
}
static void stop_race(const char*dir)
{
    for(unsigned pass=0;pass<128;pass++){
        Source s={.owner=sthread_get_current_thread_id()};
        s.bytes=load(dir,"v5.chd",&s.length);
        LRCHDSource src={&s,s.length,read_source,NULL,close_source,NULL,NULL};int error;
        LRCHD*c=lr_chd_open(&src,&error);assert(c && !error);s.live=true;
        LRCHDRequest*r[LR_CHD_SLOTS];
        for(unsigned i=0;i<LR_CHD_SLOTS;i++)assert(!lr_chd_submit(c,4096,8192,&r[i]));
        lr_chd_close(c);assert(s.closes==1);
        for(unsigned i=0;i<LR_CHD_SLOTS;i++){
            const void*p;size_t n;int e=lr_chd_poll(r[i],&p,&n);
            assert(e==1 || e==-ECANCELED);lr_chd_release(r[i]);
        }
    }
}
int main(int argc,char**argv)
{
    assert(argc==2);
    const char*names[]={"v5.chd","v5-zstd.chd","v5-lzma.chd"};
    for(unsigned i=0;i<3;i++)for(unsigned mapped=0;mapped<2;mapped++)run(argv[1],names[i],mapped);
    invalid(argv[1]);stop_race(argv[1]);
    puts("CHD worker: held provider/polls, full slots, abandonment, byte oracles, read errors, retained views and close passed");
}

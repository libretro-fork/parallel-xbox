/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <retro_timers.h>
#include "hw/xbox/nv2a/pgraph/gl/disk-writer.h"
static retro_atomic_int_t entered,release_writer;
static retro_eventcount_t entered_event,release_event;
static unsigned completed;
static void wait_flag(retro_atomic_int_t *flag,retro_eventcount_t *event)
{
    while(!retro_atomic_load_acquire_int(flag)){
        int key=retro_eventcount_prepare_wait(event);
        if(retro_atomic_load_acquire_int(flag))retro_eventcount_cancel_wait(event);
        else retro_eventcount_commit_wait(event,key);
    }
}
static void write_job(GLDiskSlot *slot)
{
    unsigned seq=*(unsigned*)slot->data;
    if(!seq){
        retro_atomic_store_release_int(&entered,1);retro_eventcount_notify(&entered_event);
        wait_flag(&release_writer,&release_event);
    }
    assert(seq==completed);
    for(unsigned i=0;i<128;i++)assert(((unsigned char*)slot->scratch)[i]==(unsigned char)(seq+i));
    assert(!strcmp(slot->base_path,"cache-root")&&!strcmp(slot->vendor,"vendor"));
    completed++;
}
static void submit_job(GLDiskWriter *w,GLDiskSlot *slot,unsigned seq)
{
    if(!slot->scratch){slot->scratch=malloc(128);slot->capacity=128;assert(slot->scratch);}
    for(unsigned i=0;i<128;i++)((unsigned char*)slot->scratch)[i]=seq+i;
    assert(slot->data && !((uintptr_t)slot->data % _Alignof(max_align_t)));
    *(unsigned*)slot->data=seq;
    slot->write=write_job;gl_disk_writer_submit(w);
}
int main(void)
{
    retro_eventcount_init(&entered_event);retro_eventcount_init(&release_event);
    retro_atomic_int_init(&entered,0);retro_atomic_int_init(&release_writer,0);
    char root[]="cache-root",vendor[]="vendor",version[]="version";
    GLDiskWriter *w=gl_disk_writer_create(root,vendor,version,sizeof(unsigned));assert(w);
    /* Creator owns path/driver strings independently of caller/context storage. */
    memset(root,'x',sizeof(root)-1);memset(vendor,'x',sizeof(vendor)-1);
    GLDiskSlot *first=gl_disk_writer_acquire(w);assert(first);submit_job(w,first,0);
    wait_flag(&entered,&entered_event);
    GLDiskSlot *second=gl_disk_writer_acquire(w);assert(second&&first!=second);
    assert(first->path && first->temporary_path && second->path && second->temporary_path);
    assert(first->path!=second->path && first->temporary_path!=second->temporary_path);
    assert(first->path_capacity==second->path_capacity && first->path_capacity>strlen(first->base_path)+64);
    strcpy(first->path,"first-path");strcpy(second->path,"second-path");
    strcpy(first->temporary_path,"first-private");strcpy(second->temporary_path,"second-private");
    assert(!strcmp(first->path,"first-path") && !strcmp(first->temporary_path,"first-private"));
    submit_job(w,second,1);
    /* This returns while the writer cannot progress. Nothing may overwrite
     * the slabs or require a live binding/renderer object for completion. */
    assert(!gl_disk_writer_acquire(w));
    assert(*(unsigned*)first->data==0&&*(unsigned*)second->data==1);
    retro_atomic_store_release_int(&release_writer,1);retro_eventcount_notify(&release_event);
    for(unsigned i=2;i<2000;i++){
        GLDiskSlot *slot;
        while(!(slot=gl_disk_writer_acquire(w)))retro_sleep(1);
        submit_job(w,slot,i);
    }
    gl_disk_writer_destroy(w);assert(completed==2000);
    /* Start beyond SIZE_MAX-4 before publishing or starting the consumer. */
    w=calloc(1,sizeof(*w)+2*sizeof(max_align_t));assert(w);w->base_path=strdup("cache-root");
    w->vendor=strdup("vendor");w->version=strdup("version");
    for(unsigned i=0;i<2;i++)w->slots[i]=(GLDiskSlot){.data=w->jobs+i*sizeof(max_align_t),.base_path=w->base_path,.vendor=w->vendor,.version=w->version};
    w->next=SIZE_MAX-3;retro_atomic_size_init(&w->read,w->next);
    retro_atomic_size_init(&w->published,w->next);retro_atomic_int_init(&w->stop,0);
    assert(retro_eventcount_init(&w->ready));w->thread=sthread_create(gl_disk_writer_run,w);assert(w->thread);
    for(unsigned i=2000;i<2064;i++){
        GLDiskSlot *slot;while(!(slot=gl_disk_writer_acquire(w)))retro_sleep(1);
        submit_job(w,slot,i);
    }
    gl_disk_writer_destroy(w);assert(completed==2064);
    for(unsigned i=0;i<32;i++){
        w=gl_disk_writer_create("cache-root","vendor","version",sizeof(unsigned));assert(w);gl_disk_writer_destroy(w);
    }
    assert(!gl_disk_writer_create("cache-root","vendor","version",0));
    assert(!gl_disk_writer_create("cache-root","vendor","version",SIZE_MAX));
    assert(!gl_disk_writer_create("cache-root","vendor","version",SIZE_MAX/2));
    gl_disk_writer_destroy(NULL);
    retro_eventcount_free(&entered_event);retro_eventcount_free(&release_event);
    puts("PASS: 2064 ordered disk jobs, held consumer/full queue, immutable slabs, counter wrap, pending shutdown drain and 32 reinitializations");
}

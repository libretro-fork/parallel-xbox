/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_GL_DISK_WRITER_H
#define NV2A_GL_DISK_WRITER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#if !defined(RETRO_ATOMIC_LOCK_FREE)
#error "GL disk writer requires a lock-free libretro-common atomic backend"
#endif

/* One render-thread producer and one disk consumer. A slot, including its
 * reusable binary slab, belongs to the producer only after read publication.
 * Job metadata is allocated with the writer, not for each save. The callback
 * must not free slot->data; read publication lends that same storage back to
 * the producer. The producer never waits for disk progress. */
typedef struct GLDiskSlot GLDiskSlot;
struct GLDiskSlot {
    void (*write)(GLDiskSlot *);
    void *data;
    void *scratch;
    size_t capacity;
    const char *base_path, *vendor, *version;
};
typedef struct GLDiskWriter {
    GLDiskSlot slots[2];
    retro_atomic_size_t read, published;
    size_t next;
    retro_atomic_int_t stop;
    retro_eventcount_t ready;
    sthread_t *thread;
    char *base_path, *vendor, *version;
    _Alignas(max_align_t) unsigned char jobs[];
} GLDiskWriter;

static GLDiskSlot *gl_disk_writer_acquire(GLDiskWriter *w)
{
    if (!w || w->next - retro_atomic_load_acquire_size(&w->read) >= 2) {
        return NULL;
    }
    return &w->slots[w->next & 1];
}
static void gl_disk_writer_submit(GLDiskWriter *w)
{
    retro_atomic_store_release_size(&w->published, ++w->next);
    retro_eventcount_notify(&w->ready);
}
static bool gl_disk_writer_pending(GLDiskWriter *w)
{
    return retro_atomic_load_relaxed_size(&w->read) !=
           retro_atomic_load_acquire_size(&w->published);
}
static void gl_disk_writer_run(void *arg)
{
    GLDiskWriter *w=arg;
    for (;;) {
        size_t read=retro_atomic_load_relaxed_size(&w->read);
        if (read != retro_atomic_load_acquire_size(&w->published)) {
            GLDiskSlot *slot=&w->slots[read & 1];
            slot->write(slot);
            retro_atomic_store_release_size(&w->read, read+1);
            continue;
        }
        if (retro_atomic_load_acquire_int(&w->stop)) {
            /* stop can become visible after the earlier empty check. */
            if (gl_disk_writer_pending(w)) continue;
            break;
        }
        int key=retro_eventcount_prepare_wait(&w->ready);
        if (gl_disk_writer_pending(w) ||
            retro_atomic_load_acquire_int(&w->stop)) {
            retro_eventcount_cancel_wait(&w->ready);
        } else {
            retro_eventcount_commit_wait(&w->ready,key);
        }
    }
}
static GLDiskWriter *gl_disk_writer_create(const char *base_path, const char *vendor,
                                             const char *version, size_t metadata_size)
{
    const size_t alignment = _Alignof(max_align_t);
    if (!metadata_size || metadata_size > SIZE_MAX - (alignment - 1)) return NULL;
    size_t stride = (metadata_size + alignment - 1) & ~(alignment - 1);
    if (stride > (SIZE_MAX - sizeof(GLDiskWriter)) / 2) return NULL;
    GLDiskWriter *w=calloc(1,sizeof(*w) + 2 * stride);
    if (!w) return NULL;
    w->base_path=malloc(strlen(base_path)+1);
    w->vendor=malloc(strlen(vendor)+1);
    w->version=malloc(strlen(version)+1);
    if (!w->base_path || !w->vendor || !w->version) {
        free(w->base_path);free(w->vendor);free(w->version);free(w);return NULL;
    }
    strcpy(w->base_path,base_path);
    strcpy(w->vendor,vendor);strcpy(w->version,version);
    for (unsigned i=0;i<2;i++) {
        w->slots[i].data=w->jobs+i*stride;
        w->slots[i].base_path=w->base_path;
        w->slots[i].vendor=w->vendor;w->slots[i].version=w->version;
    }
    retro_atomic_size_init(&w->read,0);
    retro_atomic_size_init(&w->published,0);
    retro_atomic_int_init(&w->stop,0);
    if (!retro_eventcount_init(&w->ready)) {free(w->base_path);free(w->vendor);free(w->version);free(w);return NULL;}
    w->thread=sthread_create(gl_disk_writer_run,w);
    if (!w->thread) {
        retro_eventcount_free(&w->ready);free(w->base_path);free(w->vendor);free(w->version);free(w);return NULL;
    }
    return w;
}
static void gl_disk_writer_destroy(GLDiskWriter *w)
{
    if (!w) return;
    /* Lifecycle only: finish the bounded published jobs before freeing slabs.
     * No GL object or live LRU entry is retained by those jobs. */
    retro_atomic_store_release_int(&w->stop,1);
    retro_eventcount_notify(&w->ready);
    sthread_join(w->thread);
    retro_eventcount_free(&w->ready);
    for (unsigned i=0;i<2;i++) free(w->slots[i].scratch);
    free(w->base_path);free(w->vendor);free(w->version);free(w);
}
#endif

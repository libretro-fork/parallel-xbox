/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_VK_PCACHE_WORKER_H
#define NV2A_VK_PCACHE_WORKER_H
#include <stdbool.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#ifndef RETRO_ATOMIC_LOCK_FREE
#error "Vulkan cache worker requires lock-free libretro-common atomics"
#endif

/* One renderer producer, one cache/disk owner. Optional marks coalesce while
 * a save is pending or running. No publication borrows a transient payload. */
typedef struct VkPCacheWorker {
    sthread_t *thread;
    retro_eventcount_t ready;
    retro_atomic_int_t pending, stop;
    void (*save)(void *);
    void *arg;
} VkPCacheWorker;

static void vk_pcache_worker_run(void *arg)
{
    VkPCacheWorker *w = arg;
    for (;;) {
        if (retro_atomic_load_acquire_int(&w->pending)) {
            w->save(w->arg);
            retro_atomic_store_release_int(&w->pending, 0);
            continue;
        }
        if (retro_atomic_load_acquire_int(&w->stop)) {
            /* The final publication may follow the first empty check. */
            if (retro_atomic_load_acquire_int(&w->pending)) {
                continue;
            }
            break;
        }
        int key = retro_eventcount_prepare_wait(&w->ready);
        if (retro_atomic_load_acquire_int(&w->pending) ||
            retro_atomic_load_acquire_int(&w->stop)) {
            retro_eventcount_cancel_wait(&w->ready);
        } else {
            retro_eventcount_commit_wait(&w->ready, key);
        }
    }
}

static VkPCacheWorker *vk_pcache_worker_create(void (*save)(void *), void *arg)
{
    VkPCacheWorker *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->save = save;
    w->arg = arg;
    retro_atomic_int_init(&w->pending, 0);
    retro_atomic_int_init(&w->stop, 0);
    if (!retro_eventcount_init(&w->ready)) { free(w); return NULL; }
    w->thread = sthread_create(vk_pcache_worker_run, w);
    if (!w->thread) { retro_eventcount_free(&w->ready); free(w); return NULL; }
    return w;
}

static void vk_pcache_worker_request(VkPCacheWorker *w)
{
    if (!w || retro_atomic_load_acquire_int(&w->pending)) return;
    retro_atomic_store_release_int(&w->pending, 1);
    retro_eventcount_notify(&w->ready);
}

static void vk_pcache_worker_destroy(VkPCacheWorker *w)
{
    if (!w) return;
    /* Lifecycle only: drain an accepted save before destroying its cache,
     * immutable path or reusable blob. The render path never joins. */
    retro_atomic_store_release_int(&w->stop, 1);
    retro_eventcount_notify(&w->ready);
    sthread_join(w->thread);
    retro_eventcount_free(&w->ready);
    free(w);
}
#endif

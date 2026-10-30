/*
 * The libretro core has no thread pools: its disk requests are done in
 * place on the emulator thread (block/libretro-disk.c), so no AioContext
 * ever gets a pool of I/O threads, and the load threads a state load may
 * start (migration/savevm.c) are only ever asked for by VFIO devices,
 * which the core does not have. qemu_loadvm_state still makes one at each
 * load (each frame with run-ahead): here that is nothing, not a GLib
 * thread pool, a mutex and a condition variable made and destroyed. Work
 * submitted anyway runs at once, on the caller's thread.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "block/thread-pool.h"

/* never dereferenced: what thread_pool_new hands back */
static int lr_thread_pool;

ThreadPoolAio *thread_pool_new_aio(struct AioContext *ctx)
{
    abort();
}

void thread_pool_free_aio(ThreadPoolAio *pool)
{
}

void thread_pool_update_params(ThreadPoolAio *pool, struct AioContext *ctx)
{
}

ThreadPool *thread_pool_new(void)
{
    return (ThreadPool *)&lr_thread_pool;
}

void thread_pool_free(ThreadPool *pool)
{
}

void thread_pool_submit_immediate(ThreadPool *pool, ThreadPoolFunc *func,
                                  void *opaque, GDestroyNotify opaque_destroy)
{
    func(opaque);
    if (opaque_destroy) {
        opaque_destroy(opaque);
    }
}

void thread_pool_wait(ThreadPool *pool)
{
}

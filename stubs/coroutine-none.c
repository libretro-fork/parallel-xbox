/*
 * The libretro core runs no coroutines: its disks are read and written in
 * place (block/libretro-disk.c), and the rest of what QEMU ran in them (the
 * block layer, NBD, incoming migration, the monitor) is not built. What is
 * still compiled asks only whether it is in one (never), and keeps the
 * queues of a screen dump that never waits (ui/console.c). Entering,
 * yielding or waiting in one is a bug, and aborts.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/coroutine.h"
#include "qemu/coroutine-core.h"
#include "block/aio.h"

bool qemu_in_coroutine(void)
{
    return false;
}

Coroutine *qemu_coroutine_self(void)
{
    return NULL;
}

Coroutine *qemu_coroutine_create(CoroutineEntry *entry, void *opaque)
{
    abort();
}

void qemu_coroutine_enter(Coroutine *co)
{
    abort();
}

void qemu_aio_coroutine_enter(AioContext *ctx, Coroutine *co)
{
    abort();
}

void coroutine_fn qemu_coroutine_yield(void)
{
    abort();
}

AioContext *qemu_coroutine_get_aio_context(Coroutine *co)
{
    abort();
}

void qemu_co_queue_init(CoQueue *queue)
{
    QSIMPLEQ_INIT(&queue->entries);
}

bool qemu_co_queue_empty(CoQueue *queue)
{
    return QSIMPLEQ_FIRST(&queue->entries) == NULL;
}

/* nothing ever waits in one, so there is nothing to wake */
void qemu_co_enter_all_impl(CoQueue *queue, QemuLockable *lock)
{
    assert(qemu_co_queue_empty(queue));
}

void coroutine_fn qemu_co_queue_wait_impl(CoQueue *queue, QemuLockable *lock,
                                          CoQueueWaitFlags flags)
{
    abort();
}

void coroutine_fn qemu_co_sleep_ns_wakeable(QemuCoSleep *w,
                                            QEMUClockType type, int64_t ns)
{
    abort();
}

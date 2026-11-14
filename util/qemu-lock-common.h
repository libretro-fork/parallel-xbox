/*
 * Common QEMU mutex and condition bookkeeping.
 *
 * Copyright Red Hat, Inc. 2018
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_LOCK_COMMON_H
#define QEMU_LOCK_COMMON_H

#include "qemu/thread.h"
#include "qemu/main-loop.h"

static inline void qemu_mutex_post_init(QemuMutex *mutex)
{
    mutex->initialized = true;
}

static inline void qemu_mutex_post_lock(QemuMutex *mutex,
                                        const char *file, int line)
{
    if (mutex_is_bql(mutex)) {
        bql_update_status(true);
    }
}

static inline void qemu_mutex_pre_unlock(QemuMutex *mutex,
                                         const char *file, int line)
{
    if (mutex_is_bql(mutex)) {
        bql_update_status(false);
    }
}

#endif

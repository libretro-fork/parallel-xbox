/*
 * Win32 mutex and condition primitives; worker lifecycle uses rthreads
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Author:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu-lock-common.h"

void qemu_mutex_init(QemuMutex *mutex)
{
    InitializeSRWLock(&mutex->lock);
    qemu_mutex_post_init(mutex);
}

void qemu_mutex_destroy(QemuMutex *mutex)
{
    assert(mutex->initialized);
    mutex->initialized = false;
    InitializeSRWLock(&mutex->lock);
}

void qemu_mutex_lock_impl(QemuMutex *mutex, const char *file, const int line)
{
    assert(mutex->initialized);
    AcquireSRWLockExclusive(&mutex->lock);
    qemu_mutex_post_lock(mutex, file, line);
}

int qemu_mutex_trylock_impl(QemuMutex *mutex, const char *file, const int line)
{
    int owned;

    assert(mutex->initialized);
    owned = TryAcquireSRWLockExclusive(&mutex->lock);
    if (owned) {
        qemu_mutex_post_lock(mutex, file, line);
        return 0;
    }
    return -EBUSY;
}

void qemu_mutex_unlock_impl(QemuMutex *mutex, const char *file, const int line)
{
    assert(mutex->initialized);
    qemu_mutex_pre_unlock(mutex, file, line);
    ReleaseSRWLockExclusive(&mutex->lock);
}

void qemu_rec_mutex_init(QemuRecMutex *mutex)
{
    InitializeCriticalSection(&mutex->lock);
    mutex->initialized = true;
}

void qemu_rec_mutex_destroy(QemuRecMutex *mutex)
{
    assert(mutex->initialized);
    mutex->initialized = false;
    DeleteCriticalSection(&mutex->lock);
}

void qemu_rec_mutex_lock_impl(QemuRecMutex *mutex, const char *file, int line)
{
    assert(mutex->initialized);
    EnterCriticalSection(&mutex->lock);
}

int qemu_rec_mutex_trylock_impl(QemuRecMutex *mutex, const char *file, int line)
{
    assert(mutex->initialized);
    return !TryEnterCriticalSection(&mutex->lock);
}

void qemu_rec_mutex_unlock_impl(QemuRecMutex *mutex, const char *file, int line)
{
    assert(mutex->initialized);
    LeaveCriticalSection(&mutex->lock);
}

void qemu_cond_init(QemuCond *cond)
{
    memset(cond, 0, sizeof(*cond));
    InitializeConditionVariable(&cond->var);
    cond->initialized = true;
}

void qemu_cond_destroy(QemuCond *cond)
{
    assert(cond->initialized);
    cond->initialized = false;
    InitializeConditionVariable(&cond->var);
}

void qemu_cond_signal(QemuCond *cond)
{
    assert(cond->initialized);
    WakeConditionVariable(&cond->var);
}

void qemu_cond_broadcast(QemuCond *cond)
{
    assert(cond->initialized);
    WakeAllConditionVariable(&cond->var);
}

void qemu_cond_wait_impl(QemuCond *cond, QemuMutex *mutex, const char *file, const int line)
{
    assert(cond->initialized);
    qemu_mutex_pre_unlock(mutex, file, line);
    SleepConditionVariableSRW(&cond->var, &mutex->lock, INFINITE, 0);
    qemu_mutex_post_lock(mutex, file, line);
}

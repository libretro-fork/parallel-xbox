/*
 * POSIX mutex and condition primitives; worker lifecycle uses rthreads
 *
 * Copyright Red Hat, Inc. 2009
 *
 * Author:
 *  Marcelo Tosatti <mtosatti@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu-lock-common.h"
#include "qemu/tsan.h"


static bool name_threads;

static void error_exit(int err, const char *msg)
{
    fprintf(stderr, "qemu: %s: %s\n", msg, strerror(err));
    abort();
}

void qemu_mutex_init(QemuMutex *mutex)
{
    int err;

    err = pthread_mutex_init(&mutex->lock, NULL);
    if (err)
        error_exit(err, __func__);
    qemu_mutex_post_init(mutex);
}

void qemu_mutex_destroy(QemuMutex *mutex)
{
    int err;

    assert(mutex->initialized);
    mutex->initialized = false;
    err = pthread_mutex_destroy(&mutex->lock);
    if (err)
        error_exit(err, __func__);
}

void qemu_mutex_lock_impl(QemuMutex *mutex, const char *file, const int line)
{
    int err;

    assert(mutex->initialized);
    err = pthread_mutex_lock(&mutex->lock);
    if (err)
        error_exit(err, __func__);
    qemu_mutex_post_lock(mutex, file, line);
}

int qemu_mutex_trylock_impl(QemuMutex *mutex, const char *file, const int line)
{
    int err;

    assert(mutex->initialized);
    err = pthread_mutex_trylock(&mutex->lock);
    if (err == 0) {
        qemu_mutex_post_lock(mutex, file, line);
        return 0;
    }
    if (err != EBUSY) {
        error_exit(err, __func__);
    }
    return -EBUSY;
}

void qemu_mutex_unlock_impl(QemuMutex *mutex, const char *file, const int line)
{
    int err;

    assert(mutex->initialized);
    qemu_mutex_pre_unlock(mutex, file, line);
    err = pthread_mutex_unlock(&mutex->lock);
    if (err)
        error_exit(err, __func__);
}

void qemu_rec_mutex_init(QemuRecMutex *mutex)
{
    int err;
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    err = pthread_mutex_init(&mutex->m.lock, &attr);
    pthread_mutexattr_destroy(&attr);
    if (err) {
        error_exit(err, __func__);
    }
    mutex->m.initialized = true;
}

void qemu_rec_mutex_destroy(QemuRecMutex *mutex)
{
    qemu_mutex_destroy(&mutex->m);
}

void qemu_cond_init(QemuCond *cond)
{
    pthread_condattr_t attr;
    int err;

    err = pthread_condattr_init(&attr);
    if (err) {
        error_exit(err, __func__);
    }
    err = pthread_cond_init(&cond->cond, &attr);
    if (err) {
        error_exit(err, __func__);
    }
    err = pthread_condattr_destroy(&attr);
    if (err) {
        error_exit(err, __func__);
    }
    cond->initialized = true;
}

void qemu_cond_destroy(QemuCond *cond)
{
    int err;

    assert(cond->initialized);
    cond->initialized = false;
    err = pthread_cond_destroy(&cond->cond);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_signal(QemuCond *cond)
{
    int err;

    assert(cond->initialized);
    err = pthread_cond_signal(&cond->cond);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_broadcast(QemuCond *cond)
{
    int err;

    assert(cond->initialized);
    err = pthread_cond_broadcast(&cond->cond);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_wait_impl(QemuCond *cond, QemuMutex *mutex, const char *file, const int line)
{
    int err;

    assert(cond->initialized);
    qemu_mutex_pre_unlock(mutex, file, line);
    err = pthread_cond_wait(&cond->cond, &mutex->lock);
    qemu_mutex_post_lock(mutex, file, line);
    if (err)
        error_exit(err, __func__);
}

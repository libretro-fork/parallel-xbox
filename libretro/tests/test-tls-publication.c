/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>

static retro_atomic_int_t created, deleted, pause_first, first_ready, release_first;
static bool test_create(sthread_tls_t *key)
{
    bool ok = sthread_tls_create(key);
    if (ok) {
        int n = retro_atomic_fetch_add_int(&created, 1);
        if (n == 0 && retro_atomic_load_acquire_int(&pause_first)) {
            retro_atomic_store_release_int(&first_ready, 1);
            while (!retro_atomic_load_acquire_int(&release_first)) {}
        }
    }
    return ok;
}
static bool test_delete(sthread_tls_t *key)
{
    bool ok = sthread_tls_delete(key);
    if (ok) retro_atomic_fetch_add_int(&deleted, 1);
    return ok;
}
#define sthread_tls_create test_create
#define sthread_tls_delete test_delete
#include "qemu/win32-tls.h"
#undef sthread_tls_create
#undef sthread_tls_delete

static QemuWin32TLS key = QEMU_WIN32_TLS_INITIALIZER;
static QemuWin32TLS stress[32];
static unsigned *registered_reader;
static sthread_tls_t canonical;
static retro_atomic_int_t arrived;
static void slow_first(void *unused)
{
    unsigned value = 91;
    qemu_win32_tls_set(&key, &value);
    assert(qemu_win32_tls_get(&key) == &value);
    assert(qemu_win32_tls_key(&key) == canonical);
}
static void winner(void *unused)
{
    registered_reader = calloc(1, sizeof(*registered_reader));
    assert(registered_reader);
    *registered_reader = 792;
    assert(!qemu_win32_tls_get(&key));
    qemu_win32_tls_set(&key, registered_reader);
    canonical = qemu_win32_tls_key(&key);
}
static void reader(void *opaque)
{
    unsigned id = (unsigned)(uintptr_t)opaque;
    unsigned value = id;
    assert(qemu_win32_tls_key(&key) == canonical);
    assert(!qemu_win32_tls_get(&key));
    qemu_win32_tls_set(&key, &value);
    retro_atomic_fetch_add_int(&arrived, 1);
    while (retro_atomic_load_acquire_int(&arrived) < 16) {}
    for (unsigned n = 0; n < 32; n++) {
        assert(!qemu_win32_tls_get(&stress[n]));
        qemu_win32_tls_set(&stress[n], &value);
    }
    for (unsigned n = 0; n < 10000; n++) {
        assert(qemu_win32_tls_key(&key) == canonical);
        assert(qemu_win32_tls_get(&key) == &value);
        assert(qemu_win32_tls_get(&stress[n % 32]) == &value);
    }
}
int main(void)
{
    sthread_t *slow, *fast, *threads[16];
    retro_atomic_store_release_int(&pause_first, 1);
    slow = sthread_create(slow_first, NULL);
    assert(slow);
    while (!retro_atomic_load_acquire_int(&first_ready)) {}
    fast = sthread_create(winner, NULL);
    assert(fast);
    /* The second initializer completes while the first is paused. */
    sthread_join(fast);
    assert(qemu_win32_tls_key(&key) == canonical);
    assert(*registered_reader == 792 && !qemu_win32_tls_get(&key));
    retro_atomic_store_release_int(&release_first, 1);
    sthread_join(slow);
    assert(retro_atomic_load_acquire_int(&created) == 2);
    assert(retro_atomic_load_acquire_int(&deleted) == 1);
    for (unsigned n = 0; n < 16; n++) {
        threads[n] = sthread_create(reader, (void *)(uintptr_t)(n + 1));
        assert(threads[n]);
    }
    for (unsigned n = 0; n < 16; n++) sthread_join(threads[n]);
    assert(*registered_reader == 792 && !qemu_win32_tls_get(&key));
    assert(retro_atomic_load_acquire_int(&created) -
           retro_atomic_load_acquire_int(&deleted) == 33);
    free(registered_reader);
    printf("TLS publication: paused first initializer, winner exit, losing-key cleanup, "
           "33 permanent keys and 16 isolated readers PASS\n");
    return 0;
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef QEMU_WIN32_TLS_H
#define QEMU_WIN32_TLS_H

#include <limits.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>

#ifndef RETRO_ATOMIC_LOCK_FREE
#error "TLS key publication requires lock-free retro_atomic operations"
#endif
#if defined(RETRO_ATOMIC_BACKEND_C11)
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "TLS key atomics must be lock-free");
#elif defined(RETRO_ATOMIC_BACKEND_GCC_NEW)
_Static_assert(__atomic_always_lock_free(sizeof(retro_atomic_int_t), 0),
               "TLS key atomics must be lock-free");
#endif

/* The canonical key belongs to the loaded module, never its first thread.
 * Zero means unpublished; a positive value encodes key + 1. Competing first
 * users allocate private keys, publish once, then discard losing candidates.
 * No thread waits for another initializer, and the published key never moves.
 * OS key allocation itself is provided by libretro-common, not claimed to be
 * lock-free. Windows uses the pre-Windows-8 Tls* APIs through that provider.
 */
typedef struct QemuWin32TLS {
    retro_atomic_int_t published_key;
} QemuWin32TLS;

#define QEMU_WIN32_TLS_INITIALIZER { RETRO_ATOMIC_INT_INITIALIZER(0) }

static inline sthread_tls_t qemu_win32_tls_key(QemuWin32TLS *tls)
{
    int published = retro_atomic_load_acquire_int(&tls->published_key);
    sthread_tls_t candidate;

    if (published) {
        return (unsigned)published - 1;
    }
    if (!sthread_tls_create(&candidate)) {
        abort();
    }
    /* Win32 TLS indices fit easily; make the signed encoding explicit. */
    if (candidate >= INT_MAX) {
        sthread_tls_delete(&candidate);
        abort();
    }
    if (retro_atomic_cas_int(&tls->published_key, 0, (int)candidate + 1)) {
        return candidate;
    }
    if (!sthread_tls_delete(&candidate)) {
        abort();
    }
    published = retro_atomic_load_acquire_int(&tls->published_key);
    return (unsigned)published - 1;
}

static inline void *qemu_win32_tls_get(QemuWin32TLS *tls)
{
    sthread_tls_t key = qemu_win32_tls_key(tls);
    return sthread_tls_get(&key);
}

static inline void qemu_win32_tls_set(QemuWin32TLS *tls, void *value)
{
    sthread_tls_t key = qemu_win32_tls_key(tls);
    if (!sthread_tls_set(&key, value)) {
        abort();
    }
}

#endif

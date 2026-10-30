/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef QEMU_WIN32_TLS_H
#define QEMU_WIN32_TLS_H

#include <windows.h>
#include <stdlib.h>

/* A key belongs to the loaded module, not to the first thread to use it.
 * Publishing it without a once barrier can strand live per-thread objects
 * under a replaced key (including RCU's registered reader).
 */
typedef struct QemuWin32TLS {
    INIT_ONCE once;
    DWORD key;
} QemuWin32TLS;

#define QEMU_WIN32_TLS_INITIALIZER { INIT_ONCE_STATIC_INIT, TLS_OUT_OF_INDEXES }

static BOOL CALLBACK qemu_win32_tls_init(PINIT_ONCE once, PVOID opaque,
                                       PVOID *context)
{
    QemuWin32TLS *tls = (QemuWin32TLS *)opaque;
    tls->key = TlsAlloc();
    if (tls->key == TLS_OUT_OF_INDEXES) {
        abort();
    }
    return TRUE;
}

static inline DWORD qemu_win32_tls_key(QemuWin32TLS *tls)
{
    if (!InitOnceExecuteOnce(&tls->once, qemu_win32_tls_init, tls, NULL)) {
        abort();
    }
    return tls->key;
}

static inline void *qemu_win32_tls_get(QemuWin32TLS *tls)
{
    return TlsGetValue(qemu_win32_tls_key(tls));
}

static inline void qemu_win32_tls_set(QemuWin32TLS *tls, void *value)
{
    if (!TlsSetValue(qemu_win32_tls_key(tls), value)) {
        abort();
    }
}

#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native Windows: gcc -O2 -Wall -Iinclude test-win32-tls.c -o test.exe */
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>

static LONG allocations, failures, arrived;
static HANDLE start, finish;

static DWORD delayed_tls_alloc(void)
{
    InterlockedIncrement(&allocations);
    /* Widen simultaneous first access: an unsynchronized key is replaced. */
    Sleep(20);
    return TlsAlloc();
}

#define TlsAlloc delayed_tls_alloc
#define g_malloc0(n) calloc(1, (n))
#include "qemu/thread-local.h"

QEMU_DECLARE_THREAD_LOCAL(unsigned, shared_reader)
QEMU_DEFINE_THREAD_LOCAL(unsigned, shared_reader)
QEMU_DEFINE_STATIC_THREAD_LOCAL(unsigned, private_reader)
#undef TlsAlloc

#define THREADS 32

static unsigned __stdcall reader(void *opaque)
{
    unsigned id = (unsigned)(uintptr_t)opaque;
    unsigned *a, *b;
    WaitForSingleObject(start, INFINITE);
    a = get_ptr_shared_reader();
    b = get_ptr_private_reader();
    *a = id;
    *b = id + 100;
    if (InterlockedIncrement(&arrived) == THREADS) {
        SetEvent(finish);
    }
    WaitForSingleObject(finish, INFINITE);
    for (int i = 0; i < 10000; i++) {
        if (get_ptr_shared_reader() != a || get_shared_reader() != id ||
            get_ptr_private_reader() != b || get_private_reader() != id + 100) {
            InterlockedIncrement(&failures);
            break;
        }
    }
    /* Retain the cells just as registered readers remain valid until removed. */
    return 0;
}

int main(void)
{
    HANDLE threads[THREADS];
    start = CreateEvent(NULL, TRUE, FALSE, NULL);
    finish = CreateEvent(NULL, TRUE, FALSE, NULL);
    for (unsigned i = 0; i < THREADS; i++) {
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, reader,
                                          (void *)(uintptr_t)(i + 1), 0, NULL);
        if (!threads[i]) {
            return 2;
        }
    }
    SetEvent(start);
    if (WaitForMultipleObjects(THREADS, threads, TRUE, 10000) != WAIT_OBJECT_0) {
        fprintf(stderr, "TLS concurrency test timed out\n");
        return 2;
    }
    for (unsigned i = 0; i < THREADS; i++) {
        CloseHandle(threads[i]);
    }
    CloseHandle(start);
    CloseHandle(finish);
    printf("TLS: %ld keys (expected 2), %ld unstable readers\n",
           allocations, failures);
    return allocations != 2 || failures != 0;
}

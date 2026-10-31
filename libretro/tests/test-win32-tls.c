/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native Windows: gcc -O2 -Wall -Iinclude test-win32-tls.c -o test.exe */
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>

static LONG allocations, failures, arrived;
static HANDLE start, finish;

#include <rthreads/rthreads.h>
static LONG deleted;
static bool delayed_tls_alloc(sthread_tls_t *key)
{
    bool ok = sthread_tls_create(key);
    if (ok) InterlockedIncrement(&allocations);
    Sleep(20); /* Force competing private allocations before publication. */
    return ok;
}
static bool counted_tls_delete(sthread_tls_t *key)
{
    bool ok = sthread_tls_delete(key);
    if (ok) InterlockedIncrement(&deleted);
    return ok;
}
#define sthread_tls_create delayed_tls_alloc
#define sthread_tls_delete counted_tls_delete
#define g_malloc0(n) calloc(1, (n))
#include "qemu/thread-local.h"

QEMU_DECLARE_THREAD_LOCAL(unsigned, shared_reader)
QEMU_DEFINE_THREAD_LOCAL(unsigned, shared_reader)
QEMU_DEFINE_STATIC_THREAD_LOCAL(unsigned, private_reader)
#undef sthread_tls_create
#undef sthread_tls_delete

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
    printf("TLS: %ld candidates, %ld discarded, %ld permanent keys, %ld unstable readers\n",
           allocations, deleted, allocations - deleted, failures);
    return allocations < 2 || allocations - deleted != 2 || failures != 0;
}

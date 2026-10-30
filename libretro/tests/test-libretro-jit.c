/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <memmap.h>

static __thread unsigned depth, begins, ends;

/* A strict mock of libretro-common's balanced per-thread write scopes. */
void memjit_write_begin(void)
{
    assert(depth == 0);
    depth++;
    begins++;
}

void memjit_write_end(void)
{
    assert(depth == 1);
    depth--;
    ends++;
}

void write_from_other_tu(void);
void execute_from_other_tu(void);

static void *worker(void *unused)
{
    (void)unused;
    assert(depth == 0);
    qemu_thread_jit_execute();
    assert(ends == 0);
    for (unsigned n = 0; n < 1000; n++) {
        for (unsigned repeat = 0; repeat < 8; repeat++) {
            qemu_thread_jit_write();
            write_from_other_tu();
            assert(depth == 1);
        }
        execute_from_other_tu();
        assert(depth == 0);
        qemu_thread_jit_execute();
        execute_from_other_tu();
        assert(ends == n + 1 && begins == n + 1);
    }
    return NULL;
}

int main(void)
{
    pthread_t threads[4];
    qemu_thread_jit_execute();
    qemu_thread_jit_write();
    write_from_other_tu();
    assert(depth == 1 && begins == 1 && ends == 0);
    /* Keep this thread writable while all others switch independently. */
    for (unsigned i = 0; i < 4; i++) {
        assert(pthread_create(&threads[i], NULL, worker, NULL) == 0);
    }
    for (unsigned i = 0; i < 4; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
    }
    assert(depth == 1 && begins == 1 && ends == 0);
    execute_from_other_tu();
    qemu_thread_jit_execute();
    assert(depth == 0 && begins == 1 && ends == 1);
    puts("JIT absolute modes, repeated requests, cross-TU state and thread isolation: PASS");
    return 0;
}

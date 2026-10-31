/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <rthreads/rthreads.h>
#include <retro_atomic.h>
#define g_malloc0(n) calloc(1, (n))
#include "qemu/thread-local.h"
QEMU_DECLARE_THREAD_LOCAL(unsigned, reader)
QEMU_DEFINE_THREAD_LOCAL(unsigned, reader)
QEMU_DEFINE_STATIC_THREAD_LOCAL(unsigned, owner)
static retro_atomic_int_t arrived = RETRO_ATOMIC_INT_INITIALIZER(0);
static void run(void *opaque)
{
    unsigned id = (unsigned)(uintptr_t)opaque;
    unsigned *p = get_ptr_reader();
    assert(*p == 0 && get_owner() == 0);
    set_reader(id);
    set_owner(id + 100);
    retro_atomic_fetch_add_int(&arrived, 1);
    while (retro_atomic_load_acquire_int(&arrived) < 16) {}
    for (unsigned i = 0; i < 10000; i++) {
        assert(get_ptr_reader() == p && get_reader() == id);
        assert(get_owner() == id + 100);
    }
}
int main(void)
{
    sthread_t *t[16];
    for (unsigned i = 0; i < 16; i++) {
        t[i] = sthread_create(run, (void *)(uintptr_t)(i + 1));
        assert(t[i]);
    }
    for (unsigned i = 0; i < 16; i++) sthread_join(t[i]);
    assert(get_reader() == 0 && get_owner() == 0);
    return 0;
}

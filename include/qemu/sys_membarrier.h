/*
 * Process-global memory barriers
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 */

#ifndef QEMU_SYS_MEMBARRIER_H
#define QEMU_SYS_MEMBARRIER_H

#include <retro_atomic.h>

/*
 * RCU's readers order their accesses with smp_mb_placeholder(), its
 * writer with smp_mb_global().  Where the host can make every thread of
 * the process run a full barrier at once (Linux's membarrier, Windows'
 * FlushProcessWriteBuffers), smp_mb_global() does that and the readers'
 * side, run on every rcu_read_lock() and rcu_read_unlock(), only blocks
 * reordering at the compiler level.  Elsewhere, or when the Linux kernel
 * lacks the call, both sides execute a real memory barrier.
 */
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(_WIN32)
#define QEMU_HAVE_SYS_MEMBARRIER 1

/* Set once by smp_mb_global_init(), before any other thread exists */
extern bool smp_mb_global_available;

void smp_mb_global_init(void);
void smp_mb_global(void);

static inline void smp_mb_placeholder(void)
{
    if (likely(smp_mb_global_available)) {
        barrier();
    } else {
        retro_atomic_thread_fence_seq_cst();
    }
}
#else
static inline void smp_mb_global_init(void) {}
#define smp_mb_global()            retro_atomic_thread_fence_seq_cst()
#define smp_mb_placeholder()       retro_atomic_thread_fence_seq_cst()
#endif

#endif

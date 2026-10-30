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

/* A real memory barrier on both sides.  */
static inline void smp_mb_global_init(void) {}
#define smp_mb_global()            retro_atomic_thread_fence_seq_cst()
#define smp_mb_placeholder()       retro_atomic_thread_fence_seq_cst()

#endif

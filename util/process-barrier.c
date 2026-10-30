/*
 * Process-global memory barriers
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/sys_membarrier.h"

#ifdef QEMU_HAVE_SYS_MEMBARRIER

bool smp_mb_global_available;

#ifdef _WIN32

/* Every processor running one of the process' threads takes a full
 * barrier (an inter-processor interrupt); available since Vista */
void smp_mb_global(void)
{
    if (likely(smp_mb_global_available)) {
        FlushProcessWriteBuffers();
    } else {
        retro_atomic_thread_fence_seq_cst();
    }
}

void smp_mb_global_init(void)
{
    smp_mb_global_available = true;
}

#else /* Linux */

#include <sys/syscall.h>
#include <linux/membarrier.h>

static int membarrier(int cmd)
{
    return syscall(__NR_membarrier, cmd, 0);
}

void smp_mb_global(void)
{
    if (likely(smp_mb_global_available)) {
        membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED);
    } else {
        retro_atomic_thread_fence_seq_cst();
    }
}

void smp_mb_global_init(void)
{
    int cmds = membarrier(MEMBARRIER_CMD_QUERY);

    /* The expedited command (Linux 4.14) interrupts only the CPUs that run
     * this process' threads; the process has to register for it first.
     * Without it, readers keep their own full barriers. */
    if (cmds >= 0 && (cmds & MEMBARRIER_CMD_PRIVATE_EXPEDITED) &&
        membarrier(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED) == 0) {
        smp_mb_global_available = true;
    }
}

#endif
#endif /* QEMU_HAVE_SYS_MEMBARRIER */

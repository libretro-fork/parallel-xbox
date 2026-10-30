/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_BARRIER_H
#define QEMU_BARRIER_H

#include <retro_atomic.h>

/*
 * A full barrier after a read-modify-write, where QEMU's sequentially
 * consistent read-modify-write ordered a later load (a Dekker-shaped
 * handshake): x86's read-modify-writes are full barriers already, an
 * acq_rel one elsewhere is not.
 */
#if defined(__i386__) || defined(__x86_64__)
#define qemu_mb_after_rmw() ((void)0)
#else
#define qemu_mb_after_rmw() retro_atomic_thread_fence_seq_cst()
#endif

#endif

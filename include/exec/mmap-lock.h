/*
 * QEMU user-only mmap lock, with stubs for system mode
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef EXEC_MMAP_LOCK_H
#define EXEC_MMAP_LOCK_H


static inline void mmap_lock(void) {}
static inline void mmap_unlock(void) {}
#define WITH_MMAP_LOCK_GUARD()

#endif /* EXEC_MMAP_LOCK_H */

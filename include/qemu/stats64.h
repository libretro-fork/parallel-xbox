/*
 * Atomic operations on 64-bit quantities.
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_STATS64_H
#define QEMU_STATS64_H

#include <retro_atomic.h>

/* A 64-bit counter updated from several threads.  The core runs on 64-bit
 * hosts only, so every access is a single relaxed atomic.
 */

typedef struct Stat64 {
    retro_atomic_64_t value;
} Stat64;

static inline uint64_t stat64_get(const Stat64 *s)
{
    return (uint64_t)retro_atomic_load_relaxed_64((retro_atomic_64_t *)&s->value);
}

static inline void stat64_set(Stat64 *s, uint64_t value)
{
    retro_atomic_store_relaxed_64(&s->value, (int64_t)value);
}

static inline void stat64_add(Stat64 *s, uint64_t value)
{
    retro_atomic_fetch_add_64(&s->value, (int64_t)value);
}

#endif

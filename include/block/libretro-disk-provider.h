/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLOCK_LIBRETRO_DISK_PROVIDER_H
#define BLOCK_LIBRETRO_DISK_PROVIDER_H
#include <stdbool.h>
/* Embed first in the provider's owned request context. Ordinary disk slots
 * carry only a nullable pointer, not two callbacks plus an opaque per slot. */
typedef struct LRDiskProvider LRDiskProvider;
struct LRDiskProvider {
    bool (*ready)(LRDiskProvider *provider, int *ret);
    void (*abandon)(LRDiskProvider *provider);
};
#endif

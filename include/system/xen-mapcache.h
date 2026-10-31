/*
 * Copyright (C) 2011       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef XEN_MAPCACHE_H
#define XEN_MAPCACHE_H

#include "exec/cpu-common.h"
#include "system/xen.h"

typedef hwaddr (*phys_offset_to_gaddr_t)(hwaddr phys_offset,
                                         ram_addr_t size);
uint8_t *xen_map_cache(MemoryRegion *mr, hwaddr phys_addr, hwaddr size,
                       ram_addr_t ram_addr_offset,
                       uint8_t lock, bool dma,
                       bool is_write);
ram_addr_t xen_ram_addr_from_mapcache(void *ptr);
void xen_invalidate_map_cache_entry(uint8_t *buffer);

#endif /* XEN_MAPCACHE_H */

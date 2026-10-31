/*
 * Plugin Memory API
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PLUGIN_MEMORY_H
#define PLUGIN_MEMORY_H

#include "exec/hwaddr.h"

struct qemu_plugin_hwaddr {
    bool is_io;
    bool is_store;
    hwaddr phys_addr;
    MemoryRegion *mr;
};

#endif /* PLUGIN_MEMORY_H */

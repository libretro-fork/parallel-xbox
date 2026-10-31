/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PGRAPH_SPV_SWIZZLE_H
#define PGRAPH_SPV_SWIZZLE_H
#include <stdbool.h>
#include "module.h"
#define PGRAPH_SPV_SWIZZLE_WORKGROUP 256
/* Borrow immutable words. to_ram packs a linear 2D surface into guest RAM. */
SpvStaticModule pgraph_spv_swizzle(bool to_ram);
#endif

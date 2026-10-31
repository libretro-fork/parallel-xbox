/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PGRAPH_SPV_DEPTH_H
#define PGRAPH_SPV_DEPTH_H
#include <stdbool.h>
#include "module.h"
/* Three storage buffers at bindings 0/1/2; four uint push constants:
 * input width, output width, stencil word base, output pixel count.
 * Immutable SPIR-V 1.0, with 128- or 256-thread workgroups. */
SpvStaticModule pgraph_spv_depth_compute(bool float_depth, bool pack, unsigned group_size);
#endif

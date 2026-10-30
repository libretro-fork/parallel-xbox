/*
 * QEMU Parallel PORT (ISA bus helpers)
 *
 * These functions reside in a separate file since they also might be
 * required for linking when compiling QEMU without CONFIG_PARALLEL.
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "system/system.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "hw/char/parallel-isa.h"
#include "hw/char/parallel.h"
#include "qapi/error.h"


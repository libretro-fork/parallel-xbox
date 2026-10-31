/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors: Ziqiao Zhou  <ziqiaozhou@microsoft.com>
 *          Magnus Kulke <magnuskulke@microsoft.com>
 *          Jinank Jain  <jinankjain@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef QEMU_MSHV_H
#define QEMU_MSHV_H

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "hw/hyperv/hyperv-proto.h"
#include "hw/hyperv/hvhdk.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "qapi/qapi-types-common.h"
#include "system/memory.h"
#include "accel/accel-ops.h"

#ifdef COMPILING_PER_TARGET
#else
#define CONFIG_MSHV_IS_POSSIBLE
#endif

#define MSHV_MAX_MSI_ROUTES 4096

#define MSHV_PAGE_SHIFT 12

#ifdef CONFIG_MSHV_IS_POSSIBLE
extern bool mshv_allowed;
#define mshv_enabled() (mshv_allowed)
#define mshv_msi_via_irqfd_enabled() mshv_enabled()
#else /* CONFIG_MSHV_IS_POSSIBLE */
#define mshv_enabled() false
#define mshv_msi_via_irqfd_enabled() mshv_enabled()
#endif

typedef struct MshvState MshvState;
extern MshvState *mshv_state;

/* interrupt */


#endif

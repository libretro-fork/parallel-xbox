/*
 * QEMU fw_cfg helpers (X86 specific)
 *
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "system/numa.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/firmware/smbios.h"
#include "hw/i386/fw_cfg.h"
#include "hw/timer/hpet.h"
#include "hw/nvram/fw_cfg.h"
#include "e820_memory_layout.h"
#include "kvm/kvm_i386.h"
#include "qapi/error.h"
#include CONFIG_DEVICES
#include "target/i386/cpu.h"

#if !defined(CONFIG_HPET)
struct hpet_fw_config hpet_fw_cfg = {.count = UINT8_MAX};
#endif

const char *fw_cfg_arch_key_name(uint16_t key)
{
    static const struct {
        uint16_t key;
        const char *name;
    } fw_cfg_arch_wellknown_keys[] = {
        {FW_CFG_ACPI_TABLES, "acpi_tables"},
        {FW_CFG_SMBIOS_ENTRIES, "smbios_entries"},
        {FW_CFG_IRQ0_OVERRIDE, "irq0_override"},
        {FW_CFG_HPET, "hpet"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(fw_cfg_arch_wellknown_keys); i++) {
        if (fw_cfg_arch_wellknown_keys[i].key == key) {
            return fw_cfg_arch_wellknown_keys[i].name;
        }
    }
    return NULL;
}

#ifdef CONFIG_ACPI
#endif

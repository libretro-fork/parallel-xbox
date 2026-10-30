/*
 * msi.h
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_MSI_H
#define QEMU_MSI_H

#include "hw/pci/pci_device.h"

struct MSIMessage {
    uint64_t address;
    uint32_t data;
};

extern bool msi_nonbroken;

MSIMessage msi_get_message(PCIDevice *dev, unsigned int vector);
void msi_set_enabled(PCIDevice *dev);
void msi_reset(PCIDevice *dev);
bool msi_is_masked(const PCIDevice *dev, unsigned int vector);
void msi_notify(PCIDevice *dev, unsigned int vector);
void msi_send_message(PCIDevice *dev, MSIMessage msg);
void msi_write_config(PCIDevice *dev, uint32_t addr, uint32_t val, int len);

static inline bool msi_present(const PCIDevice *dev)
{
    return dev->cap_present & QEMU_PCI_CAP_MSI;
}

#endif /* QEMU_MSI_H */

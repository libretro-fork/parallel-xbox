/*
 * pcie.c
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie.h"
#include "hw/pci/msix.h"
#include "hw/pci/msi.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pcie_regs.h"
#include "hw/pci/pcie_port.h"
#include "qemu/range.h"
#include "trace.h"

//#define DEBUG_PCIE
#ifdef DEBUG_PCIE
# define PCIE_DPRINTF(fmt, ...)                                         \
    fprintf(stderr, "%s:%d " fmt, __func__, __LINE__, ## __VA_ARGS__)
#else
# define PCIE_DPRINTF(fmt, ...) do {} while (0)
#endif
#define PCIE_DEV_PRINTF(dev, fmt, ...)                                  \
    PCIE_DPRINTF("%s:%x "fmt, (dev)->name, (dev)->devfn, ## __VA_ARGS__)

uint8_t pcie_cap_get_type(const PCIDevice *dev)
{
    uint32_t pos = dev->exp.exp_cap;
    assert(pos > 0);
    return (pci_get_word(dev->config + pos + PCI_EXP_FLAGS) &
            PCI_EXP_FLAGS_TYPE) >> PCI_EXP_FLAGS_TYPE_SHIFT;
}

/* MSI/MSI-X */
/* pci express interrupt message number */
/* 7.8.2 PCI Express Capabilities Register: Interrupt Message Number */

/**************************************************************************
 * pci express extended capability list management functions
 * uint16_t ext_cap_id (16 bit)
 * uint8_t cap_ver (4 bit)
 * uint16_t cap_offset (12 bit)
 * uint16_t ext_cap_size
 */

/* Passing a cap_id value > 0xffff will return 0 and put end of list in prev */
static uint16_t pcie_find_capability_list(PCIDevice *dev, uint32_t cap_id,
                                          uint16_t *prev_p)
{
    uint16_t prev = 0;
    uint16_t next;
    uint32_t header = pci_get_long(dev->config + PCI_CONFIG_SPACE_SIZE);

    if (!header) {
        /* no extended capability */
        next = 0;
        goto out;
    }
    for (next = PCI_CONFIG_SPACE_SIZE; next;
         prev = next, next = PCI_EXT_CAP_NEXT(header)) {

        assert(next >= PCI_CONFIG_SPACE_SIZE);
        assert(next <= PCIE_CONFIG_SPACE_SIZE - 8);

        header = pci_get_long(dev->config + next);
        if (PCI_EXT_CAP_ID(header) == cap_id) {
            break;
        }
    }

out:
    if (prev_p) {
        *prev_p = prev;
    }
    return next;
}

uint16_t pcie_find_capability(PCIDevice *dev, uint16_t cap_id)
{
    return pcie_find_capability_list(dev, cap_id, NULL);
}

/*
 * Sync the PCIe Link Status negotiated speed and width of a bridge with the
 * downstream device.  If downstream device is not present, re-write with the
 * Link Capability fields.  If downstream device reports invalid width or
 * speed, replace with minimum values (LnkSta fields are RsvdZ on VFs but such
 * values interfere with PCIe native hotplug detecting new devices).  Limit
 * width and speed to bridge capabilities for compatibility.  Use config_read
 * to access the downstream device since it could be an assigned device with
 * volatile link information.
 */
void pcie_sync_bridge_lnk(PCIDevice *bridge_dev)
{
    PCIBridge *br = PCI_BRIDGE(bridge_dev);
    PCIBus *bus = pci_bridge_get_sec_bus(br);
    PCIDevice *target = bus->devices[0];
    uint8_t *exp_cap = bridge_dev->config + bridge_dev->exp.exp_cap;
    uint16_t lnksta, lnkcap = pci_get_word(exp_cap + PCI_EXP_LNKCAP);

    if (!target || !target->exp.exp_cap) {
        lnksta = lnkcap;
    } else {
        lnksta = target->config_read(target,
                                     target->exp.exp_cap + PCI_EXP_LNKSTA,
                                     sizeof(lnksta));

        if ((lnksta & PCI_EXP_LNKSTA_NLW) > (lnkcap & PCI_EXP_LNKCAP_MLW)) {
            lnksta &= ~PCI_EXP_LNKSTA_NLW;
            lnksta |= lnkcap & PCI_EXP_LNKCAP_MLW;
        }

        if ((lnksta & PCI_EXP_LNKSTA_CLS) > (lnkcap & PCI_EXP_LNKCAP_SLS)) {
            lnksta &= ~PCI_EXP_LNKSTA_CLS;
            lnksta |= lnkcap & PCI_EXP_LNKCAP_SLS;
        }
    }

    if (!(lnksta & PCI_EXP_LNKSTA_NLW)) {
        lnksta |= QEMU_PCI_EXP_LNKSTA_NLW(QEMU_PCI_EXP_LNK_X1);
    }

    if (!(lnksta & PCI_EXP_LNKSTA_CLS)) {
        lnksta |= QEMU_PCI_EXP_LNKSTA_CLS(QEMU_PCI_EXP_LNK_2_5GT);
    }

    pci_word_test_and_clear_mask(exp_cap + PCI_EXP_LNKSTA,
                                 PCI_EXP_LNKSTA_CLS | PCI_EXP_LNKSTA_NLW);
    pci_word_test_and_set_mask(exp_cap + PCI_EXP_LNKSTA, lnksta &
                               (PCI_EXP_LNKSTA_CLS | PCI_EXP_LNKSTA_NLW));
}


/*
 * pcie.h
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

#ifndef QEMU_PCIE_H
#define QEMU_PCIE_H

#include "hw/pci/pci_regs.h"
#include "hw/pci/pcie_regs.h"
#include "hw/pci/pcie_aer.h"
#include "hw/pci/pcie_sriov.h"
#include "hw/hotplug.h"

typedef struct PCIEPort PCIEPort;
typedef struct PCIESlot PCIESlot;

typedef enum {
    /* these bits must match the bits in Slot Control/Status registers.
     * PCI_EXP_HP_EV_xxx = PCI_EXP_SLTCTL_xxxE = PCI_EXP_SLTSTA_xxx
     *
     * Not all the bits of slot control register match with the ones of
     * slot status. Not some bits of slot status register is used to
     * show status, not to report event occurrence.
     * So such bits must be masked out when checking the software
     * notification condition.
     */
    PCI_EXP_HP_EV_ABP           = PCI_EXP_SLTCTL_ABPE,
                                        /* attention button pressed */
    PCI_EXP_HP_EV_PDC           = PCI_EXP_SLTCTL_PDCE,
                                        /* presence detect changed */
    PCI_EXP_HP_EV_CCI           = PCI_EXP_SLTCTL_CCIE,
                                        /* command completed */

    PCI_EXP_HP_EV_SUPPORTED     = PCI_EXP_HP_EV_ABP |
                                  PCI_EXP_HP_EV_PDC |
                                  PCI_EXP_HP_EV_CCI,
                                                /* supported event mask  */

    /* events not listed aren't supported */
} PCIExpressHotPlugEvent;

struct PCIExpressDevice {
    /* Offset of express capability in config space */
    uint8_t exp_cap;

    /* SLOT */
    bool hpev_notified; /* Logical AND of conditions for hot plug event.
                         Following 6.7.3.4:
                         Software Notification of Hot-Plug Events, an interrupt
                         is sent whenever the logical and of these conditions
                         transitions from false to true. */

    /* AER */
    uint16_t aer_cap;
    PCIEAERLog aer_log;

    /* Offset of ATS, PRI and PASID capabilities in config space */
    uint16_t ats_cap;
    uint16_t pasid_cap;
    uint16_t pri_cap;

    /* ACS */
    uint16_t acs_cap;

    /* SR/IOV */
    uint16_t sriov_cap;
    PCIESriovPF sriov_pf;
    PCIESriovVF sriov_vf;
};

#define COMPAT_PROP_PCP "power_controller_present"

uint8_t pcie_cap_get_type(const PCIDevice *dev);
uint8_t pcie_cap_flags_get_vector(PCIDevice *dev);

/* PCI express extended capability helper functions */
uint16_t pcie_find_capability(PCIDevice *dev, uint16_t cap_id);
void pcie_sync_bridge_lnk(PCIDevice *dev);

bool pcie_pri_enabled(const PCIDevice *dev);
bool pcie_pasid_enabled(const PCIDevice *dev);
bool pcie_ats_enabled(const PCIDevice *dev);
#endif /* QEMU_PCIE_H */

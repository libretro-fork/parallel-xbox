/*
 * pcie_sriov.c:
 *
 * Implementation of SR/IOV emulation support.
 *
 * Copyright (c) 2015-2017 Knut Omang <knut.omang@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "trace.h"

static GHashTable *pfs;

static void register_vfs(PCIDevice *dev)
{
    uint16_t num_vfs;
    uint16_t i;
    uint16_t sriov_cap = dev->exp.sriov_cap;

    assert(sriov_cap > 0);
    num_vfs = pci_get_word(dev->config + sriov_cap + PCI_SRIOV_NUM_VF);

    trace_sriov_register_vfs(dev->name, PCI_SLOT(dev->devfn),
                             PCI_FUNC(dev->devfn), num_vfs);
    for (i = 0; i < num_vfs; i++) {
        pci_set_enabled(dev->exp.sriov_pf.vf[i], true);
    }

    pci_set_word(dev->wmask + sriov_cap + PCI_SRIOV_NUM_VF, 0);
}

static void unregister_vfs(PCIDevice *dev)
{
    uint8_t *cfg = dev->config + dev->exp.sriov_cap;
    uint16_t i;

    trace_sriov_unregister_vfs(dev->name, PCI_SLOT(dev->devfn),
                               PCI_FUNC(dev->devfn));
    for (i = 0; i < pci_get_word(cfg + PCI_SRIOV_TOTAL_VF); i++) {
        pci_set_enabled(dev->exp.sriov_pf.vf[i], false);
    }

    pci_set_word(dev->wmask + dev->exp.sriov_cap + PCI_SRIOV_NUM_VF, 0xffff);
}

static void consume_config(PCIDevice *dev)
{
    uint8_t *cfg = dev->config + dev->exp.sriov_cap;

    if (pci_get_word(cfg + PCI_SRIOV_CTRL) & PCI_SRIOV_CTRL_VFE) {
        register_vfs(dev);
    } else {
        uint8_t *wmask = dev->wmask + dev->exp.sriov_cap;
        uint16_t num_vfs = pci_get_word(cfg + PCI_SRIOV_NUM_VF);
        uint16_t wmask_val = PCI_SRIOV_CTRL_MSE | PCI_SRIOV_CTRL_ARI;

        unregister_vfs(dev);

        if (num_vfs <= pci_get_word(cfg + PCI_SRIOV_TOTAL_VF)) {
            wmask_val |= PCI_SRIOV_CTRL_VFE;
        }

        pci_set_word(wmask + PCI_SRIOV_CTRL, wmask_val);
    }
}

bool pcie_sriov_register_device(PCIDevice *dev, Error **errp)
{
    if (!dev->exp.sriov_pf.vf && dev->qdev.id &&
        pfs && g_hash_table_contains(pfs, dev->qdev.id)) {
        error_setg(errp, "attaching user-created SR-IOV VF unsupported");
        return false;
    }

    if (dev->sriov_pf) {
        PCIDevice *pci_pf;
        GPtrArray *pf;

        if (!PCI_DEVICE_GET_CLASS(dev)->sriov_vf_user_creatable) {
            error_setg(errp, "user cannot create SR-IOV VF with this device type");
            return false;
        }

        if (!pci_is_express(dev)) {
            error_setg(errp, "PCI Express is required for SR-IOV VF");
            return false;
        }

        if (!pci_qdev_find_device(dev->sriov_pf, &pci_pf)) {
            error_setg(errp, "PCI device specified as SR-IOV PF already exists");
            return false;
        }

        if (!pfs) {
            pfs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        }

        pf = g_hash_table_lookup(pfs, dev->sriov_pf);
        if (!pf) {
            pf = g_ptr_array_new();
            g_hash_table_insert(pfs, g_strdup(dev->sriov_pf), pf);
        }

        g_ptr_array_add(pf, dev);
    }

    return true;
}

void pcie_sriov_unregister_device(PCIDevice *dev)
{
    if (dev->sriov_pf && pfs) {
        GPtrArray *pf = g_hash_table_lookup(pfs, dev->sriov_pf);

        if (pf) {
            g_ptr_array_remove_fast(pf, dev);

            if (!pf->len) {
                g_hash_table_remove(pfs, dev->sriov_pf);
                g_ptr_array_free(pf, FALSE);
            }
        }
    }
}

void pcie_sriov_config_write(PCIDevice *dev, uint32_t address,
                             uint32_t val, int len)
{
    uint32_t off;
    uint16_t sriov_cap = dev->exp.sriov_cap;

    if (!sriov_cap || address < sriov_cap) {
        return;
    }
    off = address - sriov_cap;
    if (off >= PCI_EXT_CAP_SRIOV_SIZEOF) {
        return;
    }

    trace_sriov_config_write(dev->name, PCI_SLOT(dev->devfn),
                             PCI_FUNC(dev->devfn), off, val, len);

    consume_config(dev);
}

void pcie_sriov_pf_post_load(PCIDevice *dev)
{
    if (dev->exp.sriov_cap) {
        consume_config(dev);
    }
}


/* Reset SR/IOV */
void pcie_sriov_pf_reset(PCIDevice *dev)
{
    uint16_t sriov_cap = dev->exp.sriov_cap;
    if (!sriov_cap) {
        return;
    }

    pci_set_word(dev->config + sriov_cap + PCI_SRIOV_CTRL, 0);
    unregister_vfs(dev);

    pci_set_word(dev->config + sriov_cap + PCI_SRIOV_NUM_VF, 0);
    pci_set_word(dev->wmask + sriov_cap + PCI_SRIOV_CTRL,
                 PCI_SRIOV_CTRL_VFE | PCI_SRIOV_CTRL_MSE | PCI_SRIOV_CTRL_ARI);

    /*
     * Default is to use 4K pages, software can modify it
     * to any of the supported bits
     */
    pci_set_word(dev->config + sriov_cap + PCI_SRIOV_SYS_PGSIZE, 0x1);

    for (uint16_t i = 0; i < PCI_NUM_REGIONS; i++) {
        pci_set_quad(dev->config + sriov_cap + PCI_SRIOV_BAR + i * 4,
                     dev->exp.sriov_pf.vf_bar_type[i]);
    }
}



/*
 * MSI-X device support
 *
 * This module includes support for MSI-X in pci devices.
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 *  Copyright (c) 2009, Red Hat Inc, Michael S. Tsirkin (mst@redhat.com)
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "hw/xen/xen.h"
#include "system/xen.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "trace.h"

#include "hw/i386/kvm/xen_evtchn.h"

/* MSI enable bit and maskall bit are in byte 1 in FLAGS register */
#define MSIX_CONTROL_OFFSET (PCI_MSIX_FLAGS + 1)
#define MSIX_ENABLE_MASK (PCI_MSIX_FLAGS_ENABLE >> 8)
#define MSIX_MASKALL_MASK (PCI_MSIX_FLAGS_MASKALL >> 8)

static MSIMessage msix_prepare_message(PCIDevice *dev, unsigned vector)
{
    uint8_t *table_entry = dev->msix_table + vector * PCI_MSIX_ENTRY_SIZE;
    MSIMessage msg;

    msg.address = pci_get_quad(table_entry + PCI_MSIX_ENTRY_LOWER_ADDR);
    msg.data = pci_get_long(table_entry + PCI_MSIX_ENTRY_DATA);
    return msg;
}

MSIMessage msix_get_message(PCIDevice *dev, unsigned vector)
{
    return dev->msix_prepare_message(dev, vector);
}

static uint8_t msix_pending_mask(int vector)
{
    return 1 << (vector % 8);
}

static uint8_t *msix_pending_byte(PCIDevice *dev, int vector)
{
    return dev->msix_pba + vector / 8;
}

int msix_is_pending(PCIDevice *dev, unsigned int vector)
{
    return *msix_pending_byte(dev, vector) & msix_pending_mask(vector);
}

void msix_set_pending(PCIDevice *dev, unsigned int vector)
{
    *msix_pending_byte(dev, vector) |= msix_pending_mask(vector);
}

void msix_clr_pending(PCIDevice *dev, int vector)
{
    *msix_pending_byte(dev, vector) &= ~msix_pending_mask(vector);
}

static bool msix_vector_masked(PCIDevice *dev, unsigned int vector, bool fmask)
{
    unsigned offset = vector * PCI_MSIX_ENTRY_SIZE;
    uint8_t *data = &dev->msix_table[offset + PCI_MSIX_ENTRY_DATA];
    /* MSIs on Xen can be remapped into pirqs. In those cases, masking
     * and unmasking go through the PV evtchn path. */
    if (xen_enabled() && xen_is_pirq_msi(pci_get_long(data))) {
        return false;
    }
    return fmask || dev->msix_table[offset + PCI_MSIX_ENTRY_VECTOR_CTRL] &
        PCI_MSIX_ENTRY_CTRL_MASKBIT;
}

bool msix_is_masked(PCIDevice *dev, unsigned int vector)
{
    return msix_vector_masked(dev, vector, dev->msix_function_masked);
}

static void msix_fire_vector_notifier(PCIDevice *dev,
                                      unsigned int vector, bool is_masked)
{
    MSIMessage msg;
    int ret;

    if (!dev->msix_vector_use_notifier) {
        return;
    }
    if (is_masked) {
        dev->msix_vector_release_notifier(dev, vector);
    } else {
        msg = msix_get_message(dev, vector);
        ret = dev->msix_vector_use_notifier(dev, vector, msg);
        assert(ret >= 0);
    }
}

static void msix_handle_mask_update(PCIDevice *dev, int vector, bool was_masked)
{
    bool is_masked = msix_is_masked(dev, vector);

    if (xen_mode == XEN_EMULATE) {
        MSIMessage msg = msix_prepare_message(dev, vector);

        xen_evtchn_snoop_msi(dev, true, vector, msg.address, msg.data,
                             is_masked);
    }

    if (is_masked == was_masked) {
        return;
    }

    msix_fire_vector_notifier(dev, vector, is_masked);

    if (!is_masked && msix_is_pending(dev, vector)) {
        msix_clr_pending(dev, vector);
        msix_notify(dev, vector);
    }
}

static bool msix_masked(PCIDevice *dev)
{
    return dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] & MSIX_MASKALL_MASK;
}

static void msix_update_function_masked(PCIDevice *dev)
{
    dev->msix_function_masked = !msix_enabled(dev) || msix_masked(dev);
}

/* Handle MSI-X capability config write. */
void msix_write_config(PCIDevice *dev, uint32_t addr,
                       uint32_t val, int len)
{
    unsigned enable_pos = dev->msix_cap + MSIX_CONTROL_OFFSET;
    int vector;
    bool was_masked;

    if (!msix_present(dev) || !range_covers_byte(addr, len, enable_pos)) {
        return;
    }

    trace_msix_write_config(dev->name, msix_enabled(dev), msix_masked(dev));

    was_masked = dev->msix_function_masked;
    msix_update_function_masked(dev);

    if (!msix_enabled(dev)) {
        return;
    }

    pci_device_deassert_intx(dev);

    if (dev->msix_function_masked == was_masked) {
        return;
    }

    for (vector = 0; vector < dev->msix_entries_nr; ++vector) {
        msix_handle_mask_update(dev, vector,
                                msix_vector_masked(dev, vector, was_masked));
    }
}

static void msix_mask_all(struct PCIDevice *dev, unsigned nentries)
{
    int vector;

    for (vector = 0; vector < nentries; ++vector) {
        unsigned offset =
            vector * PCI_MSIX_ENTRY_SIZE + PCI_MSIX_ENTRY_VECTOR_CTRL;
        bool was_masked = msix_is_masked(dev, vector);

        dev->msix_table[offset] |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
        msix_handle_mask_update(dev, vector, was_masked);
    }
}

static void msix_clear_all_vectors(PCIDevice *dev)
{
    int vector;

    for (vector = 0; vector < dev->msix_entries_nr; ++vector) {
        msix_clr_pending(dev, vector);
    }
}

void msix_save(PCIDevice *dev, QEMUFile *f)
{
    unsigned n = dev->msix_entries_nr;

    if (!msix_present(dev)) {
        return;
    }

    qemu_put_buffer(f, dev->msix_table, n * PCI_MSIX_ENTRY_SIZE);
    qemu_put_buffer(f, dev->msix_pba, DIV_ROUND_UP(n, 8));
}

/* Should be called after restoring the config space. */
void msix_load(PCIDevice *dev, QEMUFile *f)
{
    unsigned n = dev->msix_entries_nr;
    unsigned int vector;

    if (!msix_present(dev)) {
        return;
    }

    msix_clear_all_vectors(dev);
    qemu_get_buffer(f, dev->msix_table, n * PCI_MSIX_ENTRY_SIZE);
    qemu_get_buffer(f, dev->msix_pba, DIV_ROUND_UP(n, 8));
    msix_update_function_masked(dev);

    for (vector = 0; vector < n; vector++) {
        msix_handle_mask_update(dev, vector, true);
    }
}

/* Does device support MSI-X? */
int msix_present(PCIDevice *dev)
{
    return dev->cap_present & QEMU_PCI_CAP_MSIX;
}

/* Is MSI-X enabled? */
int msix_enabled(PCIDevice *dev)
{
    return (dev->cap_present & QEMU_PCI_CAP_MSIX) &&
        (dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] &
         MSIX_ENABLE_MASK);
}

/* Send an MSI-X message */
void msix_notify(PCIDevice *dev, unsigned vector)
{
    MSIMessage msg;

    assert(vector < dev->msix_entries_nr);

    if (!dev->msix_entry_used[vector]) {
        return;
    }

    if (msix_is_masked(dev, vector)) {
        msix_set_pending(dev, vector);
        return;
    }

    msg = msix_get_message(dev, vector);

    msi_send_message(dev, msg);
}

void msix_reset(PCIDevice *dev)
{
    if (!msix_present(dev)) {
        return;
    }
    msix_clear_all_vectors(dev);
    dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] &=
            ~dev->wmask[dev->msix_cap + MSIX_CONTROL_OFFSET];
    memset(dev->msix_table, 0, dev->msix_entries_nr * PCI_MSIX_ENTRY_SIZE);
    memset(dev->msix_pba, 0, QEMU_ALIGN_UP(dev->msix_entries_nr, 64) / 8);
    msix_mask_all(dev, dev->msix_entries_nr);
}

static int put_msix_state(QEMUFile *f, void *pv, size_t size,
                          const VMStateField *field, JSONWriter *vmdesc)
{
    msix_save(pv, f);

    return 0;
}

static int get_msix_state(QEMUFile *f, void *pv, size_t size,
                          const VMStateField *field)
{
    msix_load(pv, f);
    return 0;
}

static const VMStateInfo vmstate_info_msix = {
    .name = "msix state",
    .get  = get_msix_state,
    .put  = put_msix_state,
};

const VMStateDescription vmstate_msix = {
    .name = "msix",
    .fields = (const VMStateField[]) {
        {
            .name         = "msix",
            .version_id   = 0,
            .field_exists = NULL,
            .size         = 0,   /* ouch */
            .info         = &vmstate_info_msix,
            .flags        = VMS_SINGLE,
            .offset       = 0,
        },
        VMSTATE_END_OF_LIST()
    }
};

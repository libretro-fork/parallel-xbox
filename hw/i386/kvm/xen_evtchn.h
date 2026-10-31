/*
 * QEMU Xen emulation: Event channel support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_XEN_EVTCHN_H
#define QEMU_XEN_EVTCHN_H

#include "hw/sysbus.h"

typedef uint32_t evtchn_port_t;


void xen_evtchn_snoop_msi(PCIDevice *dev, bool is_msix, unsigned int vector,
                          uint64_t addr, uint32_t data, bool is_masked);
void xen_evtchn_remove_pci_device(PCIDevice *dev);
struct kvm_irq_routing_entry;
bool xen_evtchn_deliver_pirq_msi(uint64_t address, uint32_t data);


/*
 * These functions mirror the libxenevtchn library API, providing the QEMU
 * backend side of "interdomain" event channels.
 */
struct xenevtchn_handle;
/* Apart from this which is a local addition */

struct evtchn_status;
struct evtchn_close;
struct evtchn_unmask;
struct evtchn_bind_virq;
struct evtchn_bind_pirq;
struct evtchn_bind_ipi;
struct evtchn_send;
struct evtchn_alloc_unbound;
struct evtchn_bind_interdomain;
struct evtchn_bind_vcpu;
struct evtchn_reset;

struct physdev_map_pirq;
struct physdev_unmap_pirq;
struct physdev_eoi;
struct physdev_irq_status_query;
struct physdev_get_free_pirq;

#endif /* QEMU_XEN_EVTCHN_H */

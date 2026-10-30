/*
 * QEMU<->ACPI BIOS PCI hotplug interface
 *
 * QEMU supports PCI hotplug via ACPI. This module
 * implements the interface between QEMU and the ACPI BIOS.
 * Interface specification - see docs/specs/acpi_pci_hotplug.rst
 *
 * Copyright (c) 2013, Red Hat Inc, Michael S. Tsirkin (mst@redhat.com)
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/acpi/pcihp.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/pci-host/i440fx.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci-bridge/xio3130_downstream.h"
#include "hw/i386/acpi-build.h"
#include "hw/acpi/acpi.h"
#include "hw/pci/pci_bus.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qom/qom-qobject.h"
#include "qobject/qnum.h"
#include "trace.h"

#define PCI_UP_BASE 0x0000
#define PCI_DOWN_BASE 0x0004
#define PCI_EJ_BASE 0x0008
#define PCI_RMV_BASE 0x000c
#define PCI_SEL_BASE 0x0010
#define PCI_AIDX_BASE 0x0014

typedef struct AcpiPciHpFind {
    int bsel;
    PCIBus *bus;
} AcpiPciHpFind;

typedef struct {
    unsigned bsel_alloc;
    bool has_bridge_hotplug;
} BSELInfo;

static Aml *aml_pci_device_dsm(void)
{
    Aml *method;

    method = aml_method("_DSM", 4, AML_SERIALIZED);
    {
        Aml *params = aml_local(0);
        Aml *pkg = aml_package(2);
        aml_append(pkg, aml_int(0));
        aml_append(pkg, aml_int(0));
        aml_append(method, aml_store(pkg, params));
        aml_append(method,
            aml_store(aml_name("BSEL"), aml_index(params, aml_int(0))));
        aml_append(method,
            aml_store(aml_name("ASUN"), aml_index(params, aml_int(1))));
        aml_append(method,
            aml_return(aml_call5("PDSM", aml_arg(0), aml_arg(1),
                                 aml_arg(2), aml_arg(3), params))
        );
    }
    return method;
}

static Aml *aml_pci_static_endpoint_dsm(PCIDevice *pdev)
{
    Aml *method;

    g_assert(pdev->acpi_index != 0);
    method = aml_method("_DSM", 4, AML_SERIALIZED);
    {
        Aml *params = aml_local(0);
        Aml *pkg = aml_package(1);
        aml_append(pkg, aml_int(pdev->acpi_index));
        aml_append(method, aml_store(pkg, params));
        aml_append(method,
            aml_return(aml_call5("EDSM", aml_arg(0), aml_arg(1),
                                 aml_arg(2), aml_arg(3), params))
        );
    }
    return method;
}

static void build_append_pcihp_notify_entry(Aml *method, int slot)
{
    Aml *if_ctx;
    int32_t devfn = PCI_DEVFN(slot, 0);

    if_ctx = aml_if(aml_and(aml_arg(0), aml_int(0x1U << slot), NULL));
    aml_append(if_ctx, aml_notify(aml_name("S%.02X", devfn), aml_arg(1)));
    aml_append(method, if_ctx);
}

static bool is_devfn_ignored_generic(const int devfn, const PCIBus *bus)
{
    const PCIDevice *pdev = bus->devices[devfn];

    if (PCI_FUNC(devfn)) {
        if (IS_PCI_BRIDGE(pdev)) {
            /*
             * Ignore only hotplugged PCI bridges on !0 functions, but
             * allow describing cold plugged bridges on all functions
             */
            if (DEVICE(pdev)->hotplugged) {
                return true;
            }
        }
    }
    return false;
}

static bool is_devfn_ignored_hotplug(const int devfn, const PCIBus *bus)
{
    PCIDevice *pdev = bus->devices[devfn];
    if (pdev) {
        return is_devfn_ignored_generic(devfn, bus) ||
               !DEVICE_GET_CLASS(pdev)->hotpluggable ||
               /* Cold plugged bridges aren't themselves hot-pluggable */
               (IS_PCI_BRIDGE(pdev) && !DEVICE(pdev)->hotplugged);
    } else { /* non populated slots */
         /*
          * hotplug is supported only for non-multifunction device
          * so generate device description only for function 0
          */
        if (PCI_FUNC(devfn) ||
            (pci_bus_is_express(bus) && PCI_SLOT(devfn) > 0)) {
            return true;
        }
    }
    return false;
}

void build_append_pcihp_slots(Aml *parent_scope, PCIBus *bus)
{
    int devfn;
    Aml *dev, *notify_method = NULL, *method;
    QObject *bsel = object_property_get_qobject(OBJECT(bus),
                        ACPI_PCIHP_PROP_BSEL, NULL);
    uint64_t bsel_val = qnum_get_uint(qobject_to(QNum, bsel));
    qobject_unref(bsel);

    aml_append(parent_scope, aml_name_decl("BSEL", aml_int(bsel_val)));
    notify_method = aml_method("DVNT", 2, AML_NOTSERIALIZED);

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        int slot = PCI_SLOT(devfn);
        int adr = slot << 16 | PCI_FUNC(devfn);

        if (is_devfn_ignored_hotplug(devfn, bus)) {
            continue;
        }

        if (bus->devices[devfn]) {
            dev = aml_scope("S%.02X", devfn);
        } else {
            dev = aml_device("S%.02X", devfn);
            aml_append(dev, aml_name_decl("_ADR", aml_int(adr)));
        }

        /*
         * Can't declare _SUN here for every device as it changes 'slot'
         * enumeration order in linux kernel, so use another variable for it
         */
        aml_append(dev, aml_name_decl("ASUN", aml_int(slot)));
        aml_append(dev, aml_pci_device_dsm());

        aml_append(dev, aml_name_decl("_SUN", aml_int(slot)));
        /* add _EJ0 to make slot hotpluggable  */
        method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
        aml_append(method,
            aml_call2("PCEJ", aml_name("BSEL"), aml_name("_SUN"))
        );
        aml_append(dev, method);

        build_append_pcihp_notify_entry(notify_method, slot);

        /* device descriptor has been composed, add it into parent context */
        aml_append(parent_scope, dev);
    }
    aml_append(parent_scope, notify_method);
}

void build_append_pci_bus_devices(Aml *parent_scope, PCIBus *bus)
{
    int devfn;
    Aml *dev;

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        /* ACPI spec: 1.0b: Table 6-2 _ADR Object Bus Types, PCI type */
        int adr = PCI_SLOT(devfn) << 16 | PCI_FUNC(devfn);
        PCIDevice *pdev = bus->devices[devfn];

        if (!pdev || is_devfn_ignored_generic(devfn, bus)) {
            continue;
        }

        /* start to compose PCI device descriptor */
        dev = aml_device("S%.02X", devfn);
        aml_append(dev, aml_name_decl("_ADR", aml_int(adr)));

        call_dev_aml_func(DEVICE(bus->devices[devfn]), dev);
        /* add _DSM if device has acpi-index set */
        if (pdev->acpi_index &&
            !object_property_get_bool(OBJECT(pdev), "hotpluggable",
                                      &error_abort)) {
            aml_append(dev, aml_pci_static_endpoint_dsm(pdev));
        }

        /* device descriptor has been composed, add it into parent context */
        aml_append(parent_scope, dev);
    }
}

const VMStateDescription vmstate_acpi_pcihp_pci_status = {
    .name = "acpi_pcihp_pci_status",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(up, AcpiPciHpPciStatus),
        VMSTATE_UINT32(down, AcpiPciHpPciStatus),
        VMSTATE_END_OF_LIST()
    }
};

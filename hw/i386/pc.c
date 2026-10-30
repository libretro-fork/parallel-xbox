/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "exec/target_page.h"
#include "hw/i386/pc.h"
#include "hw/char/serial-isa.h"
#include "hw/char/parallel.h"
#include "hw/hyperv/hv-balloon.h"
#include "hw/i386/vmport.h"
#include "system/cpus.h"
#include "hw/ide/ide-bus.h"
#include "hw/timer/hpet.h"
#include "hw/loader.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/intc/i8259.h"
#include "hw/timer/i8254.h"
#include "hw/input/i8042.h"
#include "hw/audio/pcspk.h"
#include "system/system.h"
#include "system/xen.h"
#include "system/reset.h"
#include "kvm/kvm_i386.h"
#include "kvm/tdx.h"
#include "hw/xen/xen.h"
#include "qobject/qlist.h"
#include "qemu/error-report.h"
#include "hw/acpi/cpu_hotplug.h"
#include "acpi-build.h"
#include "hw/cxl/cxl_host.h"
#include "hw/usb.h"
#include "hw/i386/intel_iommu.h"
#include "hw/net/ne2000-isa.h"
#include "hw/virtio/virtio-iommu.h"
#include "hw/virtio/virtio-md-pci.h"
#include "hw/i386/kvm/xen_overlay.h"
#include "hw/i386/kvm/xen_evtchn.h"
#include "hw/i386/kvm/xen_gnttab.h"
#include "hw/i386/kvm/xen_xenstore.h"
#include "hw/mem/memory-device.h"
#include "e820_memory_layout.h"
#include "trace.h"
#include "sev.h"
#include CONFIG_DEVICES

#ifdef CONFIG_XEN_EMU
#include "hw/xen/xen-legacy-backend.h"
#include "hw/xen/xen-bus.h"
#endif

/*
 * Helper for setting model-id for CPU models that changed model-id
 * depending on QEMU versions up to QEMU 2.4.
 */
#define PC_CPU_MODEL_IDS(v) \
    { "qemu32-" TYPE_X86_CPU, "model-id", "QEMU Virtual CPU version " v, },\
    { "qemu64-" TYPE_X86_CPU, "model-id", "QEMU Virtual CPU version " v, },\
    { "athlon-" TYPE_X86_CPU, "model-id", "QEMU Virtual CPU version " v, },

GlobalProperty pc_compat_10_1[] = {
    { "mch", "extended-tseg-mbytes", "16" },
};
const size_t pc_compat_10_1_len = G_N_ELEMENTS(pc_compat_10_1);

GlobalProperty pc_compat_10_0[] = {
    { TYPE_X86_CPU, "x-consistent-cache", "false" },
    { TYPE_X86_CPU, "x-vendor-cpuid-only-v2", "false" },
    { TYPE_X86_CPU, "x-arch-cap-always-on", "true" },
    { TYPE_X86_CPU, "x-pdcm-on-even-without-pmu", "true" },
};
const size_t pc_compat_10_0_len = G_N_ELEMENTS(pc_compat_10_0);

GlobalProperty pc_compat_9_2[] = {};
const size_t pc_compat_9_2_len = G_N_ELEMENTS(pc_compat_9_2);

GlobalProperty pc_compat_9_1[] = {
    { "ICH9-LPC", "x-smi-swsmi-timer", "off" },
    { "ICH9-LPC", "x-smi-periodic-timer", "off" },
    { TYPE_INTEL_IOMMU_DEVICE, "stale-tm", "on" },
    { TYPE_INTEL_IOMMU_DEVICE, "aw-bits", "39" },
};
const size_t pc_compat_9_1_len = G_N_ELEMENTS(pc_compat_9_1);

GlobalProperty pc_compat_9_0[] = {
    { TYPE_X86_CPU, "x-amd-topoext-features-only", "false" },
    { TYPE_X86_CPU, "x-l1-cache-per-thread", "false" },
    { TYPE_X86_CPU, "guest-phys-bits", "0" },
    { "sev-guest", "legacy-vm-type", "on" },
    { TYPE_X86_CPU, "legacy-multi-node", "on" },
};
const size_t pc_compat_9_0_len = G_N_ELEMENTS(pc_compat_9_0);

GlobalProperty pc_compat_8_2[] = {};
const size_t pc_compat_8_2_len = G_N_ELEMENTS(pc_compat_8_2);

GlobalProperty pc_compat_8_1[] = {};
const size_t pc_compat_8_1_len = G_N_ELEMENTS(pc_compat_8_1);

GlobalProperty pc_compat_8_0[] = {
    { "virtio-mem", "unplugged-inaccessible", "auto" },
};
const size_t pc_compat_8_0_len = G_N_ELEMENTS(pc_compat_8_0);

GlobalProperty pc_compat_7_2[] = {
    { "ICH9-LPC", "noreboot", "true" },
};
const size_t pc_compat_7_2_len = G_N_ELEMENTS(pc_compat_7_2);

GlobalProperty pc_compat_7_1[] = {};
const size_t pc_compat_7_1_len = G_N_ELEMENTS(pc_compat_7_1);

GlobalProperty pc_compat_7_0[] = {};
const size_t pc_compat_7_0_len = G_N_ELEMENTS(pc_compat_7_0);

GlobalProperty pc_compat_6_2[] = {
    { "virtio-mem", "unplugged-inaccessible", "off" },
};
const size_t pc_compat_6_2_len = G_N_ELEMENTS(pc_compat_6_2);

GlobalProperty pc_compat_6_1[] = {
    { TYPE_X86_CPU, "hv-version-id-build", "0x1bbc" },
    { TYPE_X86_CPU, "hv-version-id-major", "0x0006" },
    { TYPE_X86_CPU, "hv-version-id-minor", "0x0001" },
    { "ICH9-LPC", "x-keep-pci-slot-hpc", "false" },
};
const size_t pc_compat_6_1_len = G_N_ELEMENTS(pc_compat_6_1);

GlobalProperty pc_compat_6_0[] = {
    { "qemu64" "-" TYPE_X86_CPU, "family", "6" },
    { "qemu64" "-" TYPE_X86_CPU, "model", "6" },
    { "qemu64" "-" TYPE_X86_CPU, "stepping", "3" },
    { TYPE_X86_CPU, "x-vendor-cpuid-only", "off" },
    { "ICH9-LPC", ACPI_PM_PROP_ACPI_PCIHP_BRIDGE, "off" },
    { "ICH9-LPC", "x-keep-pci-slot-hpc", "true" },
};
const size_t pc_compat_6_0_len = G_N_ELEMENTS(pc_compat_6_0);

GlobalProperty pc_compat_5_2[] = {
    { "ICH9-LPC", "x-smi-cpu-hotunplug", "off" },
};
const size_t pc_compat_5_2_len = G_N_ELEMENTS(pc_compat_5_2);

GlobalProperty pc_compat_5_1[] = {
    { "ICH9-LPC", "x-smi-cpu-hotplug", "off" },
    { TYPE_X86_CPU, "kvm-msi-ext-dest-id", "off" },
};
const size_t pc_compat_5_1_len = G_N_ELEMENTS(pc_compat_5_1);

GlobalProperty pc_compat_5_0[] = {
};
const size_t pc_compat_5_0_len = G_N_ELEMENTS(pc_compat_5_0);

GlobalProperty pc_compat_4_2[] = {
    { "mch", "smbase-smram", "off" },
};
const size_t pc_compat_4_2_len = G_N_ELEMENTS(pc_compat_4_2);

GlobalProperty pc_compat_4_1[] = {};
const size_t pc_compat_4_1_len = G_N_ELEMENTS(pc_compat_4_1);

GlobalProperty pc_compat_4_0[] = {};
const size_t pc_compat_4_0_len = G_N_ELEMENTS(pc_compat_4_0);

GlobalProperty pc_compat_3_1[] = {
    { "intel-iommu", "dma-drain", "off" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "rdtscp", "off" },
    { "Opteron_G4" "-" TYPE_X86_CPU, "rdtscp", "off" },
    { "Opteron_G4" "-" TYPE_X86_CPU, "npt", "off" },
    { "Opteron_G4" "-" TYPE_X86_CPU, "nrip-save", "off" },
    { "Opteron_G5" "-" TYPE_X86_CPU, "rdtscp", "off" },
    { "Opteron_G5" "-" TYPE_X86_CPU, "npt", "off" },
    { "Opteron_G5" "-" TYPE_X86_CPU, "nrip-save", "off" },
    { "EPYC" "-" TYPE_X86_CPU, "npt", "off" },
    { "EPYC" "-" TYPE_X86_CPU, "nrip-save", "off" },
    { "EPYC-IBPB" "-" TYPE_X86_CPU, "npt", "off" },
    { "EPYC-IBPB" "-" TYPE_X86_CPU, "nrip-save", "off" },
    { "Skylake-Client" "-" TYPE_X86_CPU,      "mpx", "on" },
    { "Skylake-Client-IBRS" "-" TYPE_X86_CPU, "mpx", "on" },
    { "Skylake-Server" "-" TYPE_X86_CPU,      "mpx", "on" },
    { "Skylake-Server-IBRS" "-" TYPE_X86_CPU, "mpx", "on" },
    { "Cascadelake-Server" "-" TYPE_X86_CPU,  "mpx", "on" },
    { "Icelake-Client" "-" TYPE_X86_CPU,      "mpx", "on" },
    { "Icelake-Server" "-" TYPE_X86_CPU,      "mpx", "on" },
    { "Cascadelake-Server" "-" TYPE_X86_CPU, "stepping", "5" },
    { TYPE_X86_CPU, "x-intel-pt-auto-level", "off" },
};
const size_t pc_compat_3_1_len = G_N_ELEMENTS(pc_compat_3_1);

GlobalProperty pc_compat_3_0[] = {
    { TYPE_X86_CPU, "x-hv-synic-kvm-only", "on" },
    { "Skylake-Server" "-" TYPE_X86_CPU, "pku", "off" },
    { "Skylake-Server-IBRS" "-" TYPE_X86_CPU, "pku", "off" },
};
const size_t pc_compat_3_0_len = G_N_ELEMENTS(pc_compat_3_0);

GlobalProperty pc_compat_2_12[] = {
    { TYPE_X86_CPU, "legacy-cache", "on" },
    { TYPE_X86_CPU, "topoext", "off" },
    { "EPYC-" TYPE_X86_CPU, "xlevel", "0x8000000a" },
    { "EPYC-IBPB-" TYPE_X86_CPU, "xlevel", "0x8000000a" },
};
const size_t pc_compat_2_12_len = G_N_ELEMENTS(pc_compat_2_12);

GlobalProperty pc_compat_2_11[] = {
    { TYPE_X86_CPU, "x-migrate-smi-count", "off" },
    { "Skylake-Server" "-" TYPE_X86_CPU, "clflushopt", "off" },
};
const size_t pc_compat_2_11_len = G_N_ELEMENTS(pc_compat_2_11);

GlobalProperty pc_compat_2_10[] = {
    { TYPE_X86_CPU, "x-hv-max-vps", "0x40" },
    { "i440FX-pcihost", "x-pci-hole64-fix", "off" },
    { "q35-pcihost", "x-pci-hole64-fix", "off" },
};
const size_t pc_compat_2_10_len = G_N_ELEMENTS(pc_compat_2_10);

GlobalProperty pc_compat_2_9[] = {
    { "mch", "extended-tseg-mbytes", "0" },
};
const size_t pc_compat_2_9_len = G_N_ELEMENTS(pc_compat_2_9);

GlobalProperty pc_compat_2_8[] = {
    { TYPE_X86_CPU, "tcg-cpuid", "off" },
    { "kvmclock", "x-mach-use-reliable-get-clock", "off" },
    { "ICH9-LPC", "x-smi-broadcast", "off" },
    { TYPE_X86_CPU, "vmware-cpuid-freq", "off" },
    { "Haswell-" TYPE_X86_CPU, "stepping", "1" },
};
const size_t pc_compat_2_8_len = G_N_ELEMENTS(pc_compat_2_8);

GlobalProperty pc_compat_2_7[] = {
    { TYPE_X86_CPU, "l3-cache", "off" },
    { TYPE_X86_CPU, "full-cpuid-auto-level", "off" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "family", "15" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "model", "6" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "stepping", "1" },
    { "isa-pcspk", "migrate", "off" },
};
const size_t pc_compat_2_7_len = G_N_ELEMENTS(pc_compat_2_7);

GlobalProperty pc_compat_2_6[] = {
    { TYPE_X86_CPU, "cpuid-0xb", "off" },
    { "vmxnet3", "romfile", "" },
    { TYPE_X86_CPU, "fill-mtrr-mask", "off" },
    { "apic-common", "legacy-instance-id", "on", }
};
const size_t pc_compat_2_6_len = G_N_ELEMENTS(pc_compat_2_6);

/*
 * @PC_FW_DATA:
 * Size of the chunk of memory at the top of RAM for the BIOS ACPI tables
 * and other BIOS datastructures.
 *
 * BIOS ACPI tables: 128K. Other BIOS datastructures: less than 4K
 * reported to be used at the moment, 32K should be enough for a while.
 */
#define PC_FW_DATA (0x20000 + 0x8000)

GSIState *pc_gsi_create(qemu_irq **irqs, bool pci_enabled)
{
    GSIState *s;

    s = g_new0(GSIState, 1);
    if (kvm_ioapic_in_kernel()) {
        kvm_pc_setup_irq_routing(pci_enabled);
    }
    *irqs = qemu_allocate_irqs(gsi_handler, s, IOAPIC_NUM_PINS);

    return s;
}

/* PC cmos mappings */

#define REG_EQUIPMENT_BYTE          0x14

typedef struct check_fdc_state {
    ISADevice *floppy;
    bool multiple;
} CheckFdcState;

#define NE2000_NB_MAX 6

#define PC_ROM_MIN_VGA     0xc0000
#define PC_ROM_MIN_OPTION  0xc8000
#define PC_ROM_MAX         0xe0000
#define PC_ROM_ALIGN       0x800
#define PC_ROM_SIZE        (PC_ROM_MAX - PC_ROM_MIN_VGA)

/*
 * AMD systems with an IOMMU have an additional hole close to the
 * 1Tb, which are special GPAs that cannot be DMA mapped. Depending
 * on kernel version, VFIO may or may not let you DMA map those ranges.
 * Starting Linux v5.4 we validate it, and can't create guests on AMD machines
 * with certain memory sizes. It's also wrong to use those IOVA ranges
 * in detriment of leading to IOMMU INVALID_DEVICE_REQUEST or worse.
 * The ranges reserved for Hyper-Transport are:
 *
 * FD_0000_0000h - FF_FFFF_FFFFh
 *
 * The ranges represent the following:
 *
 * Base Address   Top Address  Use
 *
 * FD_0000_0000h FD_F7FF_FFFFh Reserved interrupt address space
 * FD_F800_0000h FD_F8FF_FFFFh Interrupt/EOI IntCtl
 * FD_F900_0000h FD_F90F_FFFFh Legacy PIC IACK
 * FD_F910_0000h FD_F91F_FFFFh System Management
 * FD_F920_0000h FD_FAFF_FFFFh Reserved Page Tables
 * FD_FB00_0000h FD_FBFF_FFFFh Address Translation
 * FD_FC00_0000h FD_FDFF_FFFFh I/O Space
 * FD_FE00_0000h FD_FFFF_FFFFh Configuration
 * FE_0000_0000h FE_1FFF_FFFFh Extended Configuration/Device Messages
 * FE_2000_0000h FF_FFFF_FFFFh Reserved
 *
 * See AMD IOMMU spec, section 2.1.2 "IOMMU Logical Topology",
 * Table 3: Special Address Controls (GPA) for more information.
 */
#define AMD_HT_START         0xfd00000000UL
#define AMD_HT_END           0xffffffffffUL
#define AMD_ABOVE_1TB_START  (AMD_HT_END + 1)
#define AMD_HT_SIZE          (AMD_ABOVE_1TB_START - AMD_HT_START)

void pc_i8259_create(ISABus *isa_bus, qemu_irq *i8259_irqs)
{
    qemu_irq *i8259;

    if (kvm_pic_in_kernel()) {
        i8259 = kvm_i8259_init(isa_bus);
    } else if (xen_enabled()) {
        i8259 = xen_interrupt_controller_init();
    } else {
        i8259 = i8259_init(isa_bus, x86_allocate_cpu_irq());
    }

    for (size_t i = 0; i < ISA_NUM_IRQS; i++) {
        i8259_irqs[i] = i8259[i];
    }

    g_free(i8259);
}

/*
 * The machine is the hotplug handler of its CPUs only: the Xbox has no
 * DIMMs, virtio memory, balloon or IOMMU to plug.
 */
static void pc_machine_device_pre_plug_cb(HotplugHandler *hotplug_dev,
                                          DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        x86_cpu_pre_plug(hotplug_dev, dev, errp);
    }
}

static void pc_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        x86_cpu_plug(hotplug_dev, dev, errp);
    }
}

static void pc_machine_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                                DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        x86_cpu_unplug_request_cb(hotplug_dev, dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug request for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void pc_machine_device_unplug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        x86_cpu_unplug_cb(hotplug_dev, dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static HotplugHandler *pc_get_hotplug_handler(MachineState *machine,
                                             DeviceState *dev)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        return HOTPLUG_HANDLER(machine);
    }

    return NULL;
}

static void pc_machine_get_vmport(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    OnOffAuto vmport = pcms->vmport;

    visit_type_OnOffAuto(v, name, &vmport, errp);
}

static void pc_machine_set_vmport(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &pcms->vmport, errp);
}

static bool pc_machine_get_fd_bootchk(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->fd_bootchk;
}

static void pc_machine_set_fd_bootchk(Object *obj, bool value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->fd_bootchk = value;
}

static bool pc_machine_get_smbus(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->smbus_enabled;
}

static void pc_machine_set_smbus(Object *obj, bool value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->smbus_enabled = value;
}

static bool pc_machine_get_sata(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->sata_enabled;
}

static void pc_machine_set_sata(Object *obj, bool value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->sata_enabled = value;
}

static bool pc_machine_get_hpet(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->hpet_enabled;
}

static void pc_machine_set_hpet(Object *obj, bool value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->hpet_enabled = value;
}

static bool pc_machine_get_i8042(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->i8042_enabled;
}

static void pc_machine_set_i8042(Object *obj, bool value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->i8042_enabled = value;
}

static bool pc_machine_get_default_bus_bypass_iommu(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->default_bus_bypass_iommu;
}

static void pc_machine_set_default_bus_bypass_iommu(Object *obj, bool value,
                                                    Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->default_bus_bypass_iommu = value;
}

static void pc_machine_get_smbios_ep(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    SmbiosEntryPointType smbios_entry_point_type = pcms->smbios_entry_point_type;

    visit_type_SmbiosEntryPointType(v, name, &smbios_entry_point_type, errp);
}

static void pc_machine_set_smbios_ep(Object *obj, Visitor *v, const char *name,
                                     void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    visit_type_SmbiosEntryPointType(v, name, &pcms->smbios_entry_point_type, errp);
}

static void pc_machine_get_max_ram_below_4g(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    uint64_t value = pcms->max_ram_below_4g;

    visit_type_size(v, name, &value, errp);
}

static void pc_machine_set_max_ram_below_4g(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    uint64_t value;

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }
    if (value > 4 * GiB) {
        error_setg(errp,
                   "Machine option 'max-ram-below-4g=%"PRIu64
                   "' expects size less than or equal to 4G", value);
        return;
    }

    if (value < 1 * MiB) {
        warn_report("Only %" PRIu64 " bytes of RAM below the 4GiB boundary,"
                    "BIOS may not work with less than 1MiB", value);
    }

    pcms->max_ram_below_4g = value;
}

static void pc_machine_get_max_fw_size(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    uint64_t value = pcms->max_fw_size;

    visit_type_size(v, name, &value, errp);
}

static void pc_machine_set_max_fw_size(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    uint64_t value;

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }

    /*
     * We don't have a theoretically justifiable exact lower bound on the base
     * address of any flash mapping. In practice, the IO-APIC MMIO range is
     * [0xFEE00000..0xFEE01000] -- see IO_APIC_DEFAULT_ADDRESS --, leaving free
     * only 18MiB-4KiB below 4GiB. For now, restrict the cumulative mapping to
     * 16MiB in size.
     */
    if (value > 16 * MiB) {
        error_setg(errp,
                   "User specified max allowed firmware size %" PRIu64 " is "
                   "greater than 16MiB. If combined firmware size exceeds "
                   "16MiB the system may not boot, or experience intermittent"
                   "stability issues.",
                   value);
        return;
    }

    pcms->max_fw_size = value;
}


static void pc_machine_initfn(Object *obj)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);

#ifdef CONFIG_VMPORT
    pcms->vmport = ON_OFF_AUTO_AUTO;
#else
    pcms->vmport = ON_OFF_AUTO_OFF;
#endif /* CONFIG_VMPORT */
    pcms->max_ram_below_4g = 0; /* use default */
    pcms->smbios_entry_point_type = pcmc->default_smbios_ep_type;
    pcms->south_bridge = pcmc->default_south_bridge;

    /* acpi build is enabled by default if machine supports it */
    pcms->acpi_build_enabled = pcmc->has_acpi_build;
    pcms->smbus_enabled = true;
    pcms->sata_enabled = true;
    pcms->i8042_enabled = true;
    pcms->max_fw_size = 8 * MiB;
#if defined(CONFIG_HPET)
    pcms->hpet_enabled = true;
#endif
    pcms->fd_bootchk = true;
    pcms->default_bus_bypass_iommu = false;

    pc_system_flash_create(pcms);
    pcms->pcspk = isa_new(TYPE_PC_SPEAKER);
    if (pcmc->pci_enabled) {
        cxl_machine_init(obj, &pcms->cxl_devices_state);
    }
}

static void pc_machine_reset(MachineState *machine, ResetType type)
{
    CPUState *cs;
    X86CPU *cpu;

    qemu_devices_reset(type);

    /* Reset APIC after devices have been reset to cancel
     * any changes that qemu_devices_reset() might have done.
     */
    CPU_FOREACH(cs) {
        cpu = X86_CPU(cs);

        x86_cpu_after_reset(cpu);
    }
}

static void pc_machine_wakeup(MachineState *machine)
{
    cpu_synchronize_all_states();
    pc_machine_reset(machine, RESET_TYPE_WAKEUP);
    cpu_synchronize_all_post_reset();
}

static void pc_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    X86MachineClass *x86mc = X86_MACHINE_CLASS(oc);
    PCMachineClass *pcmc = PC_MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    pcmc->pci_enabled = true;
    pcmc->has_acpi_build = true;
    pcmc->smbios_defaults = true;
    pcmc->gigabyte_align = true;
    pcmc->has_reserved_memory = true;
    pcmc->enforce_amd_1tb_hole = true;
    pcmc->isa_bios_alias = true;
    pcmc->pvh_enabled = true;
    pcmc->kvmclock_create_always = true;
    x86mc->apic_xrupt_override = true;
    assert(!mc->get_hotplug_handler);
    mc->get_hotplug_handler = pc_get_hotplug_handler;
    mc->auto_enable_numa_with_memhp = true;
    mc->auto_enable_numa_with_memdev = true;
    mc->has_hotpluggable_cpus = true;
    mc->default_boot_order = "cad";
    mc->block_default_type = IF_IDE;
    mc->max_cpus = 255;
    mc->reset = pc_machine_reset;
    mc->wakeup = pc_machine_wakeup;
    hc->pre_plug = pc_machine_device_pre_plug_cb;
    hc->plug = pc_machine_device_plug_cb;
    hc->unplug_request = pc_machine_device_unplug_request_cb;
    hc->unplug = pc_machine_device_unplug_cb;
    mc->default_cpu_type = TARGET_DEFAULT_CPU_TYPE;
    mc->smp_props.dies_supported = true;
    mc->smp_props.modules_supported = true;
    mc->smp_props.cache_supported[CACHE_LEVEL_AND_TYPE_L1D] = true;
    mc->smp_props.cache_supported[CACHE_LEVEL_AND_TYPE_L1I] = true;
    mc->smp_props.cache_supported[CACHE_LEVEL_AND_TYPE_L2] = true;
    mc->smp_props.cache_supported[CACHE_LEVEL_AND_TYPE_L3] = true;
    mc->default_ram_id = "pc.ram";
    pcmc->default_smbios_ep_type = SMBIOS_ENTRY_POINT_TYPE_AUTO;

    object_class_property_add(oc, PC_MACHINE_MAX_RAM_BELOW_4G, "size",
        pc_machine_get_max_ram_below_4g, pc_machine_set_max_ram_below_4g,
        NULL, NULL);
    object_class_property_set_description(oc, PC_MACHINE_MAX_RAM_BELOW_4G,
        "Maximum ram below the 4G boundary (32bit boundary)");

    object_class_property_add(oc, PC_MACHINE_VMPORT, "OnOffAuto",
        pc_machine_get_vmport, pc_machine_set_vmport,
        NULL, NULL);
    object_class_property_set_description(oc, PC_MACHINE_VMPORT,
        "Enable vmport (pc & q35)");

    object_class_property_add_bool(oc, PC_MACHINE_SMBUS,
        pc_machine_get_smbus, pc_machine_set_smbus);
    object_class_property_set_description(oc, PC_MACHINE_SMBUS,
        "Enable/disable system management bus");

    object_class_property_add_bool(oc, PC_MACHINE_SATA,
        pc_machine_get_sata, pc_machine_set_sata);
    object_class_property_set_description(oc, PC_MACHINE_SATA,
        "Enable/disable Serial ATA bus");

    object_class_property_add_bool(oc, "hpet",
        pc_machine_get_hpet, pc_machine_set_hpet);
    object_class_property_set_description(oc, "hpet",
        "Enable/disable high precision event timer emulation");

    object_class_property_add_bool(oc, PC_MACHINE_I8042,
        pc_machine_get_i8042, pc_machine_set_i8042);
    object_class_property_set_description(oc, PC_MACHINE_I8042,
        "Enable/disable Intel 8042 PS/2 controller emulation");

    object_class_property_add_bool(oc, "default-bus-bypass-iommu",
        pc_machine_get_default_bus_bypass_iommu,
        pc_machine_set_default_bus_bypass_iommu);

    object_class_property_add(oc, PC_MACHINE_MAX_FW_SIZE, "size",
        pc_machine_get_max_fw_size, pc_machine_set_max_fw_size,
        NULL, NULL);
    object_class_property_set_description(oc, PC_MACHINE_MAX_FW_SIZE,
        "Maximum combined firmware size");

    object_class_property_add(oc, PC_MACHINE_SMBIOS_EP, "str",
        pc_machine_get_smbios_ep, pc_machine_set_smbios_ep,
        NULL, NULL);
    object_class_property_set_description(oc, PC_MACHINE_SMBIOS_EP,
        "SMBIOS Entry Point type [32, 64]");

    object_class_property_add_bool(oc, "fd-bootchk",
        pc_machine_get_fd_bootchk,
        pc_machine_set_fd_bootchk);

#if defined(CONFIG_IGVM)
    object_class_property_add_link(oc, "igvm-cfg",
                                   TYPE_IGVM_CFG,
                                   offsetof(X86MachineState, igvm),
                                   object_property_allow_set_link,
                                   OBJ_PROP_LINK_STRONG);
    object_class_property_set_description(oc, "igvm-cfg",
                                          "Set IGVM configuration");
#endif


}

static const TypeInfo pc_machine_info = {
    .name = TYPE_PC_MACHINE,
    .parent = TYPE_X86_MACHINE,
    .abstract = true,
    .instance_size = sizeof(PCMachineState),
    .instance_init = pc_machine_initfn,
    .class_size = sizeof(PCMachineClass),
    .class_init = pc_machine_class_init,
    .interfaces = (const InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    },
};

static void pc_machine_register_types(void)
{
    type_register_static(&pc_machine_info);
}

type_init(pc_machine_register_types)

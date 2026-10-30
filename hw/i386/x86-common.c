/*
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2019, 2024 Red Hat, Inc.
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
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "system/numa.h"
#include "system/system.h"
#include "system/xen.h"
#include "trace.h"

#include "hw/i386/x86.h"
#include "target/i386/cpu.h"
#include "hw/rtc/mc146818rtc.h"
#include "target/i386/sev.h"

#include "hw/acpi/cpu_hotplug.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "elf.h"
#include "standard-headers/asm-x86/bootparam.h"
#include CONFIG_DEVICES
#include "kvm/kvm_i386.h"
#include "kvm/tdx.h"

#ifdef CONFIG_XEN_EMU
#include "hw/xen/xen.h"
#include "hw/i386/kvm/xen_evtchn.h"
#endif

/* Physical Address of PVH entry point read from kernel ELF NOTE */

static void x86_cpu_new(X86MachineState *x86ms, int64_t apic_id, Error **errp)
{
    Object *cpu = object_new(MACHINE(x86ms)->cpu_type);

    if (!object_property_set_uint(cpu, "apic-id", apic_id, errp)) {
        goto out;
    }
    qdev_realize(DEVICE(cpu), NULL, errp);

out:
    object_unref(cpu);
}

void x86_cpus_init(X86MachineState *x86ms, int default_cpu_version)
{
    int i;
    const CPUArchIdList *possible_cpus;
    MachineState *ms = MACHINE(x86ms);
    MachineClass *mc = MACHINE_GET_CLASS(x86ms);

    x86_cpu_set_default_version(default_cpu_version);

    /*
     * Calculates the limit to CPU APIC ID values
     *
     * Limit for the APIC ID value, so that all
     * CPU APIC IDs are < x86ms->apic_id_limit.
     *
     * This is used for FW_CFG_MAX_CPUS. See comments on fw_cfg_arch_create().
     */
    x86ms->apic_id_limit = x86_cpu_apic_id_from_index(x86ms,
                                                      ms->smp.max_cpus - 1) + 1;

    /*
     * Can we support APIC ID 255 or higher?  With KVM, that requires
     * both in-kernel lapic and X2APIC userspace API.
     *
     * kvm_enabled() must go first to ensure that kvm_* references are
     * not emitted for the linker to consume (kvm_enabled() is
     * a literal `0` in configurations where kvm_* aren't defined)
     */
    if (kvm_enabled() && x86ms->apic_id_limit > 255 &&
        kvm_irqchip_in_kernel() && !kvm_enable_x2apic()) {
        error_report("current -smp configuration requires kernel "
                     "irqchip and X2APIC API support.");
        exit(EXIT_FAILURE);
    }

    if (kvm_enabled()) {
        kvm_set_max_apic_id(x86ms->apic_id_limit);
    }

    if (!kvm_irqchip_in_kernel()) {
        apic_set_max_apic_id(x86ms->apic_id_limit);
    }

    possible_cpus = mc->possible_cpu_arch_ids(ms);
    for (i = 0; i < ms->smp.cpus; i++) {
        x86_cpu_new(x86ms, possible_cpus->cpus[i].arch_id, &error_fatal);
    }
}

void x86_rtc_set_cpus_count(ISADevice *s, uint16_t cpus_count)
{
    MC146818RtcState *rtc = MC146818_RTC(s);

    if (cpus_count > 0xff) {
        /*
         * If the number of CPUs can't be represented in 8 bits, the
         * BIOS must use "FW_CFG_NB_CPUS". Set RTC field to 0 just
         * to make old BIOSes fail more predictably.
         */
        mc146818rtc_set_cmos_data(rtc, 0x5f, 0);
    } else {
        mc146818rtc_set_cmos_data(rtc, 0x5f, cpus_count - 1);
    }
}

static int x86_apic_cmp(const void *a, const void *b)
{
   CPUArchId *apic_a = (CPUArchId *)a;
   CPUArchId *apic_b = (CPUArchId *)b;

   return apic_a->arch_id - apic_b->arch_id;
}

/*
 * returns pointer to CPUArchId descriptor that matches CPU's apic_id
 * in ms->possible_cpus->cpus, if ms->possible_cpus->cpus has no
 * entry corresponding to CPU's apic_id returns NULL.
 */
static CPUArchId *x86_find_cpu_slot(MachineState *ms, uint32_t id, int *idx)
{
    CPUArchId apic_id, *found_cpu;

    apic_id.arch_id = id;
    found_cpu = bsearch(&apic_id, ms->possible_cpus->cpus,
        ms->possible_cpus->len, sizeof(*ms->possible_cpus->cpus),
        x86_apic_cmp);
    if (found_cpu && idx) {
        *idx = found_cpu - ms->possible_cpus->cpus;
    }
    return found_cpu;
}

void x86_cpu_plug(HotplugHandler *hotplug_dev,
                  DeviceState *dev, Error **errp)
{
    CPUArchId *found_cpu;
    Error *local_err = NULL;
    X86CPU *cpu = X86_CPU(dev);
    X86MachineState *x86ms = X86_MACHINE(hotplug_dev);

    if (x86ms->acpi_dev) {
        hotplug_handler_plug(x86ms->acpi_dev, dev, &local_err);
        if (local_err) {
            goto out;
        }
    }

    /* increment the number of CPUs */
    x86ms->boot_cpus++;
    if (x86ms->rtc) {
        x86_rtc_set_cpus_count(x86ms->rtc, x86ms->boot_cpus);
    }

    /*
     * Non-hotplugged CPUs get their SMM cpu address space initialized in
     * machine init done notifier: register_smram_listener().
     *
     * We need initialize the SMM cpu address space for the hotplugged CPU
     * specifically.
     */
    if (kvm_enabled() && dev->hotplugged && x86_machine_is_smm_enabled(x86ms)) {
        kvm_smm_cpu_address_space_init(cpu);
    }

    found_cpu = x86_find_cpu_slot(MACHINE(x86ms), cpu->apic_id, NULL);
    found_cpu->cpu = CPU(dev);
out:
    error_propagate(errp, local_err);
}

void x86_cpu_unplug_request_cb(HotplugHandler *hotplug_dev,
                               DeviceState *dev, Error **errp)
{
    int idx = -1;
    X86CPU *cpu = X86_CPU(dev);
    X86MachineState *x86ms = X86_MACHINE(hotplug_dev);

    if (!x86ms->acpi_dev) {
        error_setg(errp, "CPU hot unplug not supported without ACPI");
        return;
    }

    x86_find_cpu_slot(MACHINE(x86ms), cpu->apic_id, &idx);
    assert(idx != -1);
    if (idx == 0) {
        error_setg(errp, "Boot CPU is unpluggable");
        return;
    }

    hotplug_handler_unplug_request(x86ms->acpi_dev, dev,
                                   errp);
}

void x86_cpu_unplug_cb(HotplugHandler *hotplug_dev,
                       DeviceState *dev, Error **errp)
{
    CPUArchId *found_cpu;
    Error *local_err = NULL;
    X86CPU *cpu = X86_CPU(dev);
    X86MachineState *x86ms = X86_MACHINE(hotplug_dev);

    hotplug_handler_unplug(x86ms->acpi_dev, dev, &local_err);
    if (local_err) {
        goto out;
    }

    found_cpu = x86_find_cpu_slot(MACHINE(x86ms), cpu->apic_id, NULL);
    found_cpu->cpu = NULL;
    qdev_unrealize(dev);

    /* decrement the number of CPUs */
    x86ms->boot_cpus--;
    /* Update the number of CPUs in CMOS */
    x86_rtc_set_cpus_count(x86ms->rtc, x86ms->boot_cpus);
 out:
    error_propagate(errp, local_err);
}

void x86_cpu_pre_plug(HotplugHandler *hotplug_dev,
                      DeviceState *dev, Error **errp)
{
    int idx;
    CPUState *cs;
    CPUArchId *cpu_slot;
    X86CPUTopoIDs topo_ids;
    X86CPU *cpu = X86_CPU(dev);
    CPUX86State *env = &cpu->env;
    MachineState *ms = MACHINE(hotplug_dev);
    X86MachineState *x86ms = X86_MACHINE(hotplug_dev);
    X86CPUTopoInfo *topo_info = &env->topo_info;

    if (!object_dynamic_cast(OBJECT(cpu), ms->cpu_type)) {
        error_setg(errp, "Invalid CPU type, expected cpu type: '%s'",
                   ms->cpu_type);
        return;
    }

    if (x86ms->acpi_dev) {
        Error *local_err = NULL;

        hotplug_handler_pre_plug(HOTPLUG_HANDLER(x86ms->acpi_dev), dev,
                                 &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    init_topo_info(topo_info, x86ms);

    if (ms->smp.modules > 1) {
        set_bit(CPU_TOPOLOGY_LEVEL_MODULE, env->avail_cpu_topo);
    }

    if (ms->smp.dies > 1) {
        set_bit(CPU_TOPOLOGY_LEVEL_DIE, env->avail_cpu_topo);
    }

    /*
     * If APIC ID is not set,
     * set it based on socket/die/module/core/thread properties.
     */
    if (cpu->apic_id == UNASSIGNED_APIC_ID) {
        /*
         * die-id was optional in QEMU 4.0 and older, so keep it optional
         * if there's only one die per socket.
         */
        if (cpu->die_id < 0 && ms->smp.dies == 1) {
            cpu->die_id = 0;
        }

        /*
         * module-id was optional in QEMU 9.0 and older, so keep it optional
         * if there's only one module per die.
         */
        if (cpu->module_id < 0 && ms->smp.modules == 1) {
            cpu->module_id = 0;
        }

        if (cpu->socket_id < 0) {
            error_setg(errp, "CPU socket-id is not set");
            return;
        } else if (cpu->socket_id > ms->smp.sockets - 1) {
            error_setg(errp, "Invalid CPU socket-id: %u must be in range 0:%u",
                       cpu->socket_id, ms->smp.sockets - 1);
            return;
        }
        if (cpu->die_id < 0) {
            error_setg(errp, "CPU die-id is not set");
            return;
        } else if (cpu->die_id > ms->smp.dies - 1) {
            error_setg(errp, "Invalid CPU die-id: %u must be in range 0:%u",
                       cpu->die_id, ms->smp.dies - 1);
            return;
        }
        if (cpu->module_id < 0) {
            error_setg(errp, "CPU module-id is not set");
            return;
        } else if (cpu->module_id > ms->smp.modules - 1) {
            error_setg(errp, "Invalid CPU module-id: %u must be in range 0:%u",
                       cpu->module_id, ms->smp.modules - 1);
            return;
        }
        if (cpu->core_id < 0) {
            error_setg(errp, "CPU core-id is not set");
            return;
        } else if (cpu->core_id > (ms->smp.cores - 1)) {
            error_setg(errp, "Invalid CPU core-id: %u must be in range 0:%u",
                       cpu->core_id, ms->smp.cores - 1);
            return;
        }
        if (cpu->thread_id < 0) {
            error_setg(errp, "CPU thread-id is not set");
            return;
        } else if (cpu->thread_id > (ms->smp.threads - 1)) {
            error_setg(errp, "Invalid CPU thread-id: %u must be in range 0:%u",
                       cpu->thread_id, ms->smp.threads - 1);
            return;
        }

        topo_ids.pkg_id = cpu->socket_id;
        topo_ids.die_id = cpu->die_id;
        topo_ids.module_id = cpu->module_id;
        topo_ids.core_id = cpu->core_id;
        topo_ids.smt_id = cpu->thread_id;
        cpu->apic_id = x86_apicid_from_topo_ids(topo_info, &topo_ids);
    }

    cpu_slot = x86_find_cpu_slot(MACHINE(x86ms), cpu->apic_id, &idx);
    if (!cpu_slot) {
        x86_topo_ids_from_apicid(cpu->apic_id, topo_info, &topo_ids);

        error_setg(errp,
            "Invalid CPU [socket: %u, die: %u, module: %u, core: %u, thread: %u]"
            " with APIC ID %" PRIu32 ", valid index range 0:%d",
            topo_ids.pkg_id, topo_ids.die_id, topo_ids.module_id,
            topo_ids.core_id, topo_ids.smt_id, cpu->apic_id,
            ms->possible_cpus->len - 1);
        return;
    }

    if (cpu_slot->cpu) {
        error_setg(errp, "CPU[%d] with APIC ID %" PRIu32 " exists",
                   idx, cpu->apic_id);
        return;
    }

    /* if 'address' properties socket-id/core-id/thread-id are not set, set them
     * so that machine_query_hotpluggable_cpus would show correct values
     */
    /* TODO: move socket_id/core_id/thread_id checks into x86_cpu_realizefn()
     * once -smp refactoring is complete and there will be CPU private
     * CPUState::nr_cores and CPUState::nr_threads fields instead of globals */
    x86_topo_ids_from_apicid(cpu->apic_id, topo_info, &topo_ids);
    if (cpu->socket_id != -1 && cpu->socket_id != topo_ids.pkg_id) {
        error_setg(errp, "property socket-id: %u doesn't match set apic-id:"
            " 0x%x (socket-id: %u)", cpu->socket_id, cpu->apic_id,
            topo_ids.pkg_id);
        return;
    }
    cpu->socket_id = topo_ids.pkg_id;

    if (cpu->die_id != -1 && cpu->die_id != topo_ids.die_id) {
        error_setg(errp, "property die-id: %u doesn't match set apic-id:"
            " 0x%x (die-id: %u)", cpu->die_id, cpu->apic_id, topo_ids.die_id);
        return;
    }
    cpu->die_id = topo_ids.die_id;

    if (cpu->module_id != -1 && cpu->module_id != topo_ids.module_id) {
        error_setg(errp, "property module-id: %u doesn't match set apic-id:"
            " 0x%x (module-id: %u)", cpu->module_id, cpu->apic_id,
            topo_ids.module_id);
        return;
    }
    cpu->module_id = topo_ids.module_id;

    if (cpu->core_id != -1 && cpu->core_id != topo_ids.core_id) {
        error_setg(errp, "property core-id: %u doesn't match set apic-id:"
            " 0x%x (core-id: %u)", cpu->core_id, cpu->apic_id,
            topo_ids.core_id);
        return;
    }
    cpu->core_id = topo_ids.core_id;

    if (cpu->thread_id != -1 && cpu->thread_id != topo_ids.smt_id) {
        error_setg(errp, "property thread-id: %u doesn't match set apic-id:"
            " 0x%x (thread-id: %u)", cpu->thread_id, cpu->apic_id,
            topo_ids.smt_id);
        return;
    }
    cpu->thread_id = topo_ids.smt_id;

    /*
    * kvm_enabled() must go first to ensure that kvm_* references are
    * not emitted for the linker to consume (kvm_enabled() is
    * a literal `0` in configurations where kvm_* aren't defined)
    */
    if (kvm_enabled() && hyperv_feat_enabled(cpu, HYPERV_FEAT_VPINDEX) &&
        !kvm_hv_vpindex_settable()) {
        error_setg(errp, "kernel doesn't allow setting HyperV VP_INDEX");
        return;
    }

    cs = CPU(cpu);
    cs->cpu_index = idx;

    numa_cpu_pre_plug(cpu_slot, dev, errp);
}

void gsi_handler(void *opaque, int n, int level)
{
    GSIState *s = opaque;
    bool bypass_ioapic = false;

    trace_x86_gsi_interrupt(n, level);

#ifdef CONFIG_XEN_EMU
    /*
     * Xen delivers the GSI to the Legacy PIC (not that Legacy PIC
     * routing actually works properly under Xen). And then to
     * *either* the PIRQ handling or the I/OAPIC depending on whether
     * the former wants it.
     *
     * Additionally, this hook allows the Xen event channel GSI to
     * work around QEMU's lack of support for shared level interrupts,
     * by keeping track of the externally driven state of the pin and
     * implementing a logical OR with the state of the evtchn GSI.
     */
    if (xen_mode == XEN_EMULATE) {
        bypass_ioapic = xen_evtchn_set_gsi(n, &level);
    }
#endif

    switch (n) {
    case 0 ... ISA_NUM_IRQS - 1:
        if (s->i8259_irq[n]) {
            /* Under KVM, Kernel will forward to both PIC and IOAPIC */
            qemu_set_irq(s->i8259_irq[n], level);
        }
        /* fall through */
    case ISA_NUM_IRQS ... IOAPIC_NUM_PINS - 1:
        if (!bypass_ioapic) {
            qemu_set_irq(s->ioapic_irq[n], level);
        }
        break;
    case IO_APIC_SECONDARY_IRQBASE
        ... IO_APIC_SECONDARY_IRQBASE + IOAPIC_NUM_PINS - 1:
        qemu_set_irq(s->ioapic2_irq[n - IO_APIC_SECONDARY_IRQBASE], level);
        break;
    }
}


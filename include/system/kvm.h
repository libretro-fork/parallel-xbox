/*
 * QEMU KVM support
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* header to be included in non-KVM-specific code */

#ifndef QEMU_KVM_H
#define QEMU_KVM_H

#include "exec/memattrs.h"
#include "qemu/accel.h"
#include "qom/object.h"

#ifdef COMPILING_PER_TARGET
#else
# define CONFIG_KVM_IS_POSSIBLE
#endif

#ifdef CONFIG_KVM_IS_POSSIBLE

extern bool kvm_allowed;
extern bool kvm_kernel_irqchip;
extern bool kvm_split_irqchip;
extern bool kvm_async_interrupts_allowed;
extern bool kvm_halt_in_kernel_allowed;
extern bool kvm_resamplefds_allowed;
extern bool kvm_msi_via_irqfd_allowed;
extern bool kvm_gsi_routing_allowed;
extern bool kvm_gsi_direct_mapping;
extern bool kvm_readonly_mem_allowed;
extern bool kvm_msi_use_devid;
extern bool kvm_pre_fault_memory_supported;

#define kvm_enabled()           (kvm_allowed)
/**
 * kvm_irqchip_in_kernel:
 *
 * Returns: true if an in-kernel irqchip was created.
 * What this actually means is architecture and machine model
 * specific: on PC, for instance, it means that the LAPIC
 * is in kernel.  This function should never be used from generic
 * target-independent code: use one of the following functions or
 * some other specific check instead.
 */
#define kvm_irqchip_in_kernel() (kvm_kernel_irqchip)

/**
 * kvm_irqchip_is_split:
 *
 * Returns: true if the irqchip implementation is split between
 * user and kernel space.  The details are architecture and
 * machine specific.  On PC, it means that the PIC, IOAPIC, and
 * PIT are in user space while the LAPIC is in the kernel.
 */
#define kvm_irqchip_is_split() (kvm_split_irqchip)

/**
 * kvm_async_interrupts_enabled:
 *
 * Returns: true if we can deliver interrupts to KVM
 * asynchronously (ie by ioctl from any thread at any time)
 * rather than having to do interrupt delivery synchronously
 * (where the vcpu must be stopped at a suitable point first).
 */
#define kvm_async_interrupts_enabled() (kvm_async_interrupts_allowed)

/**
 * kvm_halt_in_kernel
 *
 * Returns: true if halted cpus should still get a KVM_RUN ioctl to run
 * inside of kernel space. This only works if MP state is implemented.
 */
#define kvm_halt_in_kernel() (kvm_halt_in_kernel_allowed)

/**
 * kvm_irqfds_enabled:
 *
 * Returns: true if we can use irqfds to inject interrupts into
 * a KVM CPU (ie the kernel supports irqfds and we are running
 * with a configuration where it is meaningful to use them).
 *
 * Always available if running with in-kernel irqchip.
 */
#define kvm_irqfds_enabled() kvm_irqchip_in_kernel()

/**
 * kvm_resamplefds_enabled:
 *
 * Returns: true if we can use resamplefds to inject interrupts into
 * a KVM CPU (ie the kernel supports resamplefds and we are running
 * with a configuration where it is meaningful to use them).
 */
#define kvm_resamplefds_enabled() (kvm_resamplefds_allowed)

/**
 * kvm_msi_via_irqfd_enabled:
 *
 * Returns: true if we can route a PCI MSI (Message Signaled Interrupt)
 * to a KVM CPU via an irqfd. This requires that the kernel supports
 * this and that we're running in a configuration that permits it.
 */
#define kvm_msi_via_irqfd_enabled() (kvm_msi_via_irqfd_allowed)

/**
 * kvm_gsi_routing_enabled:
 *
 * Returns: true if GSI routing is enabled (ie the kernel supports
 * it and we're running in a configuration that permits it).
 */
#define kvm_gsi_routing_enabled() (kvm_gsi_routing_allowed)

/**
 * kvm_gsi_direct_mapping:
 *
 * Returns: true if GSI direct mapping is enabled.
 */
#define kvm_gsi_direct_mapping() (kvm_gsi_direct_mapping)

/**
 * kvm_readonly_mem_enabled:
 *
 * Returns: true if KVM readonly memory is enabled (ie the kernel
 * supports it and we're running in a configuration that permits it).
 */
#define kvm_readonly_mem_enabled() (kvm_readonly_mem_allowed)

/**
 * kvm_msi_devid_required:
 * Returns: true if KVM requires a device id to be provided while
 * defining an MSI routing entry.
 */
#define kvm_msi_devid_required() (kvm_msi_use_devid)

#else

#define kvm_enabled()           (0)
#define kvm_irqchip_in_kernel() (false)
#define kvm_irqchip_is_split() (false)
#define kvm_async_interrupts_enabled() (false)
#define kvm_halt_in_kernel() (false)
#define kvm_irqfds_enabled() (false)
#define kvm_resamplefds_enabled() (false)
#define kvm_msi_via_irqfd_enabled() (false)
#define kvm_gsi_routing_allowed() (false)
#define kvm_gsi_direct_mapping() (false)
#define kvm_readonly_mem_enabled() (false)
#define kvm_msi_devid_required() (false)

#endif  /* CONFIG_KVM_IS_POSSIBLE */

struct kvm_run;
struct kvm_irq_routing_entry;

typedef struct KVMCapabilityInfo {
    const char *name;
    int value;
} KVMCapabilityInfo;

#define KVM_CAP_INFO(CAP) { "KVM_CAP_" stringify(CAP), KVM_CAP_##CAP }
#define KVM_CAP_LAST_INFO { NULL, 0 }

struct KVMState;

#define TYPE_KVM_ACCEL ACCEL_CLASS_NAME("kvm")
typedef struct KVMState KVMState;
DECLARE_INSTANCE_CHECKER(KVMState, KVM_STATE,
                         TYPE_KVM_ACCEL)

extern KVMState *kvm_state;
typedef struct Notifier Notifier;

typedef struct KVMRouteChange {
     KVMState *s;
     int changes;
} KVMRouteChange;

/* external API */

bool kvm_has_sync_mmu(void);


int kvm_on_sigbus_vcpu(CPUState *cpu, int code, void *addr);
int kvm_on_sigbus(int code, void *addr);

int kvm_vm_ioctl(KVMState *s, unsigned long type, ...);

void kvm_flush_coalesced_mmio_buffer(void);

#ifdef COMPILING_PER_TARGET
#include "cpu.h"

/**
 * kvm_update_guest_debug(): ensure KVM debug structures updated
 * @cs: the CPUState for this cpu
 * @reinject_trap: KVM trap injection control
 *
 * There are usually per-arch specifics which will be handled by
 * calling down to kvm_arch_update_guest_debug after the generic
 * fields have been set.
 */
#ifdef TARGET_KVM_HAVE_GUEST_DEBUG
int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap);
#else
static inline int kvm_update_guest_debug(CPUState *cpu, unsigned long reinject_trap)
{
    return -EINVAL;
}
#endif

/* internal API */

int kvm_vcpu_ioctl(CPUState *cpu, unsigned long type, ...);


/**
 * kvm_create_device - create a KVM device for the device control API
 * @KVMState: The KVMState pointer
 * @type: The KVM device type (see Documentation/virtual/kvm/devices in the
 *        kernel source)
 * @test: If true, only test if device can be created, but don't actually
 *        create the device.
 *
 * Returns: -errno on error, nonnegative on success: @test ? 0 : device fd;
 */
int kvm_create_device(KVMState *s, uint64_t type, bool test);

/* Arch specific hooks */

extern const KVMCapabilityInfo kvm_arch_required_capabilities[];

void kvm_arch_pre_run(CPUState *cpu, struct kvm_run *run);


typedef enum kvm_put_state {
    /* state subset only touched by the VCPU itself during runtime */
    KVM_PUT_RUNTIME_STATE = 1,
    /* state subset modified during VCPU reset */
    KVM_PUT_RESET_STATE = 2,
    /* full state set, modified during initialization or on vmload */
    KVM_PUT_FULL_STATE = 3,
} KvmPutState;



#ifdef TARGET_KVM_HAVE_RESET_PARKED_VCPU
void kvm_arch_reset_parked_vcpu(unsigned long vcpu_id, int kvm_fd);
#else
static inline void kvm_arch_reset_parked_vcpu(unsigned long vcpu_id, int kvm_fd)
{
}
#endif

/* Returns VCPU ID to be used on KVM_CREATE_VCPU ioctl() */

/* Notify arch about newly added MSI routes */
/* Notify arch about released MSI routes */


struct kvm_guest_debug;
struct kvm_debug_exit_arch;

struct kvm_sw_breakpoint {
    vaddr pc;
    vaddr saved_insn;
    int use_count;
    QTAILQ_ENTRY(kvm_sw_breakpoint) entry;
};




#define kvm_vm_enable_cap(s, capability, cap_flags, ...)             \
    ({                                                               \
        struct kvm_enable_cap cap = {                                \
            .cap = capability,                                       \
            .flags = cap_flags,                                      \
        };                                                           \
        uint64_t args_tmp[] = { __VA_ARGS__ };                       \
        size_t n = MIN(ARRAY_SIZE(args_tmp), ARRAY_SIZE(cap.args));  \
        memcpy(cap.args, args_tmp, n * sizeof(cap.args[0]));         \
        kvm_vm_ioctl(s, KVM_ENABLE_CAP, &cap);                       \
    })

#define kvm_vcpu_enable_cap(cpu, capability, cap_flags, ...)         \
    ({                                                               \
        struct kvm_enable_cap cap = {                                \
            .cap = capability,                                       \
            .flags = cap_flags,                                      \
        };                                                           \
        uint64_t args_tmp[] = { __VA_ARGS__ };                       \
        size_t n = MIN(ARRAY_SIZE(args_tmp), ARRAY_SIZE(cap.args));  \
        memcpy(cap.args, args_tmp, n * sizeof(cap.args[0]));         \
        kvm_vcpu_ioctl(cpu, KVM_ENABLE_CAP, &cap);                   \
    })

#endif /* COMPILING_PER_TARGET */


void kvm_irqchip_commit_routes(KVMState *s);

static inline KVMRouteChange kvm_irqchip_begin_route_changes(KVMState *s)
{
    return (KVMRouteChange) { .s = s, .changes = 0 };
}

static inline void kvm_irqchip_commit_route_changes(KVMRouteChange *c)
{
    if (c->changes) {
        kvm_irqchip_commit_routes(c->s);
        c->changes = 0;
    }
}




/* Notify resamplefd for EOI of specific interrupts. */

bool kvm_dirty_ring_enabled(void);

int kvm_create_guest_memfd(uint64_t size, uint64_t flags, Error **errp);

int kvm_set_memory_attributes_private(hwaddr start, uint64_t size);

#endif

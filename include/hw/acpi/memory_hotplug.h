#ifndef QEMU_HW_ACPI_MEMORY_HOTPLUG_H
#define QEMU_HW_ACPI_MEMORY_HOTPLUG_H

#include "qapi/qapi-types-acpi.h"
#include "hw/qdev-core.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"

#define MEMORY_SLOT_SCAN_METHOD      "MSCN"
#define MEMORY_DEVICES_CONTAINER     "\\_SB.MHPC"
#define MEMORY_HOTPLUG_IO_LEN         24

/**
 * MemStatus:
 * @is_removing: the memory device in slot has been requested to be ejected.
 *
 * This structure stores memory device's status.
 */
typedef struct MemStatus {
    DeviceState *dimm;
    bool is_enabled;
    bool is_inserting;
    bool is_removing;
    uint32_t ost_event;
    uint32_t ost_status;
} MemStatus;

typedef struct MemHotplugState {
    bool is_enabled; /* true if memory hotplug is supported */
    MemoryRegion io;
    uint32_t selector;
    uint32_t dev_count;
    MemStatus *devs;
} MemHotplugState;


extern const VMStateDescription vmstate_memory_hotplug;
#define VMSTATE_MEMORY_HOTPLUG(memhp, state) \
    VMSTATE_STRUCT(memhp, state, 1, \
                   vmstate_memory_hotplug, MemHotplugState)

#endif

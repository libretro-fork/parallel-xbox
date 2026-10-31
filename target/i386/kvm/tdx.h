/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_I386_TDX_H
#define QEMU_I386_TDX_H

#include CONFIG_DEVICES /* CONFIG_TDX */

#include "confidential-guest.h"
#include "cpu.h"
#include "hw/i386/tdvf.h"

#include "tdx-quote-generator.h"

#define TYPE_TDX_GUEST "tdx-guest"
#define TDX_GUEST(obj)  OBJECT_CHECK(TdxGuest, (obj), TYPE_TDX_GUEST)

typedef struct TdxGuestClass {
    X86ConfidentialGuestClass parent_class;
} TdxGuestClass;

/* TDX requires bus frequency 25MHz */
#define TDX_APIC_BUS_CYCLES_NS 40

#define TDVMCALL_GET_TD_VM_CALL_INFO    0x10000
#define TDVMCALL_GET_QUOTE		 0x10002
#define TDVMCALL_SETUP_EVENT_NOTIFY_INTERRUPT   0x10004

#define TDG_VP_VMCALL_SUCCESS           0x0000000000000000ULL
#define TDG_VP_VMCALL_RETRY             0x0000000000000001ULL
#define TDG_VP_VMCALL_INVALID_OPERAND   0x8000000000000000ULL
#define TDG_VP_VMCALL_GPA_INUSE         0x8000000000000001ULL
#define TDG_VP_VMCALL_ALIGN_ERROR       0x8000000000000002ULL

#define TDG_VP_VMCALL_SUBFUNC_SET_EVENT_NOTIFY_INTERRUPT BIT_ULL(1)

enum TdxRamType {
    TDX_RAM_UNACCEPTED,
    TDX_RAM_ADDED,
};

typedef struct TdxRamEntry {
    uint64_t address;
    uint64_t length;
    enum TdxRamType type;
} TdxRamEntry;

typedef struct TdxGuest {
    X86ConfidentialGuest parent_obj;

    QemuMutex lock;

    bool initialized;
    uint64_t attributes;    /* TD attributes */
    uint64_t xfam;
    char *mrconfigid;       /* base64 encoded sha384 digest */
    char *mrowner;          /* base64 encoded sha384 digest */
    char *mrownerconfig;    /* base64 encoded sha384 digest */

    MemoryRegion *tdvf_mr;
    TdxFirmware tdvf;

    uint32_t nr_ram_entries;
    TdxRamEntry *ram_entries;

    /* GetQuote */
    SocketAddress *qg_sock_addr;
    int num;

    uint32_t event_notify_vector;
    uint32_t event_notify_apicid;
} TdxGuest;

#define is_tdx_vm() 0


#endif /* QEMU_I386_TDX_H */

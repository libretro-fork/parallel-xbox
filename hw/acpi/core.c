/*
 * ACPI implementation
 *
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
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/acpi/acpi.h"
#include "qemu/config-file.h"
#include "qapi/error.h"
#include "qapi/opts-visitor.h"
#include "qapi/qapi-events-run-state.h"
#include "qapi/qapi-visit-acpi.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "system/runstate.h"
#include "trace.h"

struct acpi_table_header {
    uint16_t _length;         /* our length, not actual part of the hdr */
                              /* allows easier parsing for fw_cfg clients */
    char sig[4]
             QEMU_NONSTRING;  /* ACPI signature (4 ASCII characters) */
    uint32_t length;          /* Length of table, in bytes, including header */
    uint8_t revision;         /* ACPI Specification minor version # */
    uint8_t checksum;         /* To make sum of entire table == 0 */
    char oem_id[6]
             QEMU_NONSTRING;  /* OEM identification */
    char oem_table_id[8]
             QEMU_NONSTRING;  /* OEM table identification */
    uint32_t oem_revision;    /* OEM revision number */
    char asl_compiler_id[4]
             QEMU_NONSTRING;  /* ASL compiler vendor ID */
    uint32_t asl_compiler_revision; /* ASL compiler revision number */
} QEMU_PACKED;

#define ACPI_TABLE_HDR_SIZE sizeof(struct acpi_table_header)
#define ACPI_TABLE_PFX_SIZE sizeof(uint16_t)  /* size of the extra prefix */

char unsigned *acpi_tables;
size_t acpi_tables_len;


static void acpi_notify_wakeup(Notifier *notifier, void *data)
{
    ACPIREGS *ar = container_of(notifier, ACPIREGS, wakeup);
    WakeupReason *reason = data;

    switch (*reason) {
    case QEMU_WAKEUP_REASON_RTC:
        ar->pm1.evt.sts |=
            (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_RT_CLOCK_STATUS);
        break;
    case QEMU_WAKEUP_REASON_PMTIMER:
        ar->pm1.evt.sts |=
            (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_TIMER_STATUS);
        break;
    case QEMU_WAKEUP_REASON_OTHER:
        /* ACPI_BITMASK_WAKE_STATUS should be set on resume.
           Pretend that resume was caused by power button */
        ar->pm1.evt.sts |=
            (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_POWER_BUTTON_STATUS);
        break;
    default:
        break;
    }
}

/* ACPI PM1a EVT */
uint16_t acpi_pm1_evt_get_sts(ACPIREGS *ar)
{
    /* Compare ns-clock, not PM timer ticks, because
       acpi_pm_tmr_update function uses ns for setting the timer. */
    int64_t d = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (d >= muldiv64(ar->tmr.overflow_time,
                      NANOSECONDS_PER_SECOND, PM_TIMER_FREQUENCY)) {
        ar->pm1.evt.sts |= ACPI_BITMASK_TIMER_STATUS;
    }
    return ar->pm1.evt.sts;
}

static void acpi_pm1_evt_write_sts(ACPIREGS *ar, uint16_t val)
{
    uint16_t pm1_sts = acpi_pm1_evt_get_sts(ar);
    if (pm1_sts & val & ACPI_BITMASK_TIMER_STATUS) {
        /* if TMRSTS is reset, then compute the new overflow time */
        acpi_pm_tmr_calc_overflow_time(ar);
    }
    ar->pm1.evt.sts &= ~val;
}

static void acpi_pm1_evt_write_en(ACPIREGS *ar, uint16_t val)
{
    ar->pm1.evt.en = val;
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_RTC,
                              val & ACPI_BITMASK_RT_CLOCK_ENABLE);
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_PMTIMER,
                              val & ACPI_BITMASK_TIMER_ENABLE);
}

void acpi_pm1_evt_reset(ACPIREGS *ar)
{
    ar->pm1.evt.sts = 0;
    ar->pm1.evt.en = 0;
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_RTC, 0);
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_PMTIMER, 0);
}

static uint64_t acpi_pm_evt_read(void *opaque, hwaddr addr, unsigned width)
{
    ACPIREGS *ar = opaque;
    switch (addr) {
    case 0:
        return acpi_pm1_evt_get_sts(ar);
    case 2:
        return ar->pm1.evt.en;
    default:
        return 0;
    }
}

static void acpi_pm_evt_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    ACPIREGS *ar = opaque;
    switch (addr) {
    case 0:
        acpi_pm1_evt_write_sts(ar, val);
        ar->pm1.evt.update_sci(ar);
        break;
    case 2:
        acpi_pm1_evt_write_en(ar, val);
        ar->pm1.evt.update_sci(ar);
        break;
    }
}

static const MemoryRegionOps acpi_pm_evt_ops = {
    .read = acpi_pm_evt_read,
    .write = acpi_pm_evt_write,
    .impl.min_access_size = 2,
    .valid.min_access_size = 1,
    .valid.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void acpi_pm1_evt_init(ACPIREGS *ar, acpi_update_sci_fn update_sci,
                       MemoryRegion *parent)
{
    ar->pm1.evt.update_sci = update_sci;
    memory_region_init_io(&ar->pm1.evt.io, memory_region_owner(parent),
                          &acpi_pm_evt_ops, ar, "acpi-evt", 4);
    memory_region_add_subregion(parent, 0, &ar->pm1.evt.io);
}

/* ACPI PM_TMR */
void acpi_pm_tmr_update(ACPIREGS *ar, bool enable)
{
    int64_t expire_time;

    /* schedule a timer interruption if needed */
    if (enable) {
        expire_time = muldiv64(ar->tmr.overflow_time, NANOSECONDS_PER_SECOND,
                               PM_TIMER_FREQUENCY);
        timer_mod(ar->tmr.timer, expire_time);
    } else {
        timer_del(ar->tmr.timer);
    }
}

static inline int64_t acpi_pm_tmr_get_clock(void)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), PM_TIMER_FREQUENCY,
                    NANOSECONDS_PER_SECOND);
}

void acpi_pm_tmr_calc_overflow_time(ACPIREGS *ar)
{
    int64_t d = acpi_pm_tmr_get_clock();
    ar->tmr.overflow_time = (d + 0x80000000LL) & ~0x7fffffffLL;
}

static uint32_t acpi_pm_tmr_get(ACPIREGS *ar)
{
    uint32_t d = acpi_pm_tmr_get_clock();
    return d;
}

static void acpi_pm_tmr_timer(void *opaque)
{
    ACPIREGS *ar = opaque;

    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_PMTIMER, NULL);
    ar->tmr.update_sci(ar);
}

static uint64_t acpi_pm_tmr_read(void *opaque, hwaddr addr, unsigned width)
{
    return acpi_pm_tmr_get(opaque);
}

static void acpi_pm_tmr_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    /* nothing */
}

static const MemoryRegionOps acpi_pm_tmr_ops = {
    .read = acpi_pm_tmr_read,
    .write = acpi_pm_tmr_write,
    .impl.min_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void acpi_pm_tmr_init(ACPIREGS *ar, acpi_update_sci_fn update_sci,
                      MemoryRegion *parent)
{
    ar->tmr.update_sci = update_sci;
    ar->tmr.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, acpi_pm_tmr_timer, ar);
    memory_region_init_io(&ar->tmr.io, memory_region_owner(parent),
                          &acpi_pm_tmr_ops, ar, "acpi-tmr", 4);
    memory_region_enable_lockless_io(&ar->tmr.io);
    memory_region_add_subregion(parent, 8, &ar->tmr.io);
}

void acpi_pm_tmr_reset(ACPIREGS *ar)
{
    ar->tmr.overflow_time = 0;
    timer_del(ar->tmr.timer);
}

static uint64_t acpi_pm_cnt_read(void *opaque, hwaddr addr, unsigned width)
{
    ACPIREGS *ar = opaque;
    return ar->pm1.cnt.cnt >> addr * 8;
}

static void acpi_pm_cnt_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    ACPIREGS *ar = opaque;

    if (addr == 1) {
        val = val << 8 | (ar->pm1.cnt.cnt & 0xff);
    }
    ar->pm1.cnt.cnt = val & ~(ACPI_BITMASK_SLEEP_ENABLE);

    if (val & ACPI_BITMASK_SLEEP_ENABLE) {
        /* change suspend type */
        uint16_t sus_typ = (val >> 10) & 7;
        switch (sus_typ) {
        case 0: /* soft power off */
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            break;
        case 1:
            qemu_system_suspend_request();
            break;
        default:
            if (sus_typ == ar->pm1.cnt.s4_val) { /* S4 request */
                qapi_event_send_suspend_disk();
                qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            }
            break;
        }
    }
}

static const MemoryRegionOps acpi_pm_cnt_ops = {
    .read = acpi_pm_cnt_read,
    .write = acpi_pm_cnt_write,
    .impl.min_access_size = 2,
    .valid.min_access_size = 1,
    .valid.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void acpi_pm1_cnt_init(ACPIREGS *ar, MemoryRegion *parent,
                       bool disable_s3, bool disable_s4, uint8_t s4_val,
                       bool acpi_only)
{
    ar->pm1.cnt.s4_val = s4_val;
    ar->pm1.cnt.acpi_only = acpi_only;
    ar->wakeup.notify = acpi_notify_wakeup;
    qemu_register_wakeup_notifier(&ar->wakeup);

    /*
     * Register wake-up support in QMP query-current-machine API
     */
    qemu_register_wakeup_support();

    memory_region_init_io(&ar->pm1.cnt.io, memory_region_owner(parent),
                          &acpi_pm_cnt_ops, ar, "acpi-cnt", 2);
    memory_region_add_subregion(parent, 4, &ar->pm1.cnt.io);
}

void acpi_pm1_cnt_reset(ACPIREGS *ar)
{
    ar->pm1.cnt.cnt = 0;
    if (ar->pm1.cnt.acpi_only) {
        ar->pm1.cnt.cnt |= ACPI_BITMASK_SCI_ENABLE;
    }
}

/* ACPI GPE */
void acpi_gpe_init(ACPIREGS *ar, uint8_t len)
{
    ar->gpe.len = len;
    /* Only first len / 2 bytes are ever used,
     * but the caller in ich9.c migrates full len bytes.
     * TODO: fix ich9.c and drop the extra allocation.
     */
    ar->gpe.sts = g_malloc0(len);
    ar->gpe.en = g_malloc0(len);
}

void acpi_gpe_reset(ACPIREGS *ar)
{
    memset(ar->gpe.sts, 0, ar->gpe.len / 2);
    memset(ar->gpe.en, 0, ar->gpe.len / 2);
}

static uint8_t *acpi_gpe_ioport_get_ptr(ACPIREGS *ar, uint32_t addr)
{
    uint8_t *cur = NULL;

    if (addr < ar->gpe.len / 2) {
        cur = ar->gpe.sts + addr;
    } else if (addr < ar->gpe.len) {
        cur = ar->gpe.en + addr - ar->gpe.len / 2;
    } else {
        abort();
    }

    return cur;
}

void acpi_gpe_ioport_writeb(ACPIREGS *ar, uint32_t addr, uint32_t val)
{
    uint8_t *cur;

    cur = acpi_gpe_ioport_get_ptr(ar, addr);
    if (addr < ar->gpe.len / 2) {
        trace_acpi_gpe_sts_ioport_writeb(addr, val);
        /* GPE_STS */
        *cur = (*cur) & ~val;
    } else if (addr < ar->gpe.len) {
        trace_acpi_gpe_en_ioport_writeb(addr - (ar->gpe.len / 2), val);
        /* GPE_EN */
        *cur = val;
    } else {
        abort();
    }
}

uint32_t acpi_gpe_ioport_readb(ACPIREGS *ar, uint32_t addr)
{
    uint8_t *cur;
    uint32_t val;

    cur = acpi_gpe_ioport_get_ptr(ar, addr);
    val = 0;
    if (cur != NULL) {
        val = *cur;
    }

    if (addr < ar->gpe.len / 2) {
        trace_acpi_gpe_sts_ioport_readb(addr, val);
    } else {
        trace_acpi_gpe_en_ioport_readb(addr - (ar->gpe.len / 2), val);
    }

    return val;
}

void acpi_send_gpe_event(ACPIREGS *ar, qemu_irq irq,
                         AcpiEventStatusBits status)
{
    ar->gpe.sts[0] |= status;
    acpi_update_sci(ar, irq);
}

void acpi_update_sci(ACPIREGS *regs, qemu_irq irq)
{
    int sci_level, pm1a_sts;

    pm1a_sts = acpi_pm1_evt_get_sts(regs);

    sci_level = ((pm1a_sts &
                  regs->pm1.evt.en & ACPI_BITMASK_PM1_COMMON_ENABLED) != 0) ||
                ((regs->gpe.sts[0] & regs->gpe.en[0]) != 0);

    qemu_set_irq(irq, sci_level);

    /* schedule a timer interruption if needed */
    acpi_pm_tmr_update(regs,
                       (regs->pm1.evt.en & ACPI_BITMASK_TIMER_ENABLE) &&
                       !(pm1a_sts & ACPI_BITMASK_TIMER_STATUS));
}

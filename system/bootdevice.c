/*
 * QEMU Boot Device Implement
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
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
#include "qapi/error.h"
#include "system/system.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "system/reset.h"
#include "hw/qdev-core.h"
#include "hw/boards.h"

typedef struct FWBootEntry FWBootEntry;

struct FWBootEntry {
    QTAILQ_ENTRY(FWBootEntry) link;
    int32_t bootindex;
    DeviceState *dev;
    char *suffix;
};

static QTAILQ_HEAD(, FWBootEntry) fw_boot_order =
    QTAILQ_HEAD_INITIALIZER(fw_boot_order);
static QEMUBootSetHandler *boot_set_handler;
static void *boot_set_opaque;

void qemu_boot_set(const char *boot_order, Error **errp)
{
    Error *local_err = NULL;

    if (!boot_set_handler) {
        error_setg(errp, "no function defined to set boot device list for"
                         " this architecture");
        return;
    }

    validate_bootdevices(boot_order, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    boot_set_handler(boot_set_opaque, boot_order, errp);
}

void validate_bootdevices(const char *devices, Error **errp)
{
    /* We just do some generic consistency checks */
    const char *p;
    int bitmap = 0;

    for (p = devices; *p != '\0'; p++) {
        /* Allowed boot devices are:
         * a-b: floppy disk drives
         * c-f: IDE disk drives
         * g-m: machine implementation dependent drives
         * n-p: network devices
         * It's up to each machine implementation to check if the given boot
         * devices match the actual hardware implementation and firmware
         * features.
         */
        if (*p < 'a' || *p > 'p') {
            error_setg(errp, "Invalid boot device '%c'", *p);
            return;
        }
        if (bitmap & (1 << (*p - 'a'))) {
            error_setg(errp, "Boot device '%c' was given twice", *p);
            return;
        }
        bitmap |= 1 << (*p - 'a');
    }
}

void restore_boot_order(void *opaque)
{
    char *normal_boot_order = opaque;
    static int bootcount;

    switch (bootcount++) {
    case 0:
        /* First boot: use the one-time config */
        return;
    case 1:
        /* Second boot: restore normal boot order */
        if (boot_set_handler) {
            qemu_boot_set(normal_boot_order, &error_abort);
        }
        g_free(normal_boot_order);
        return;
    default:
        /* Subsequent boots: keep using normal boot order */
        return;
    }
}

void check_boot_index(int32_t bootindex, Error **errp)
{
    FWBootEntry *i;

    if (bootindex >= 0) {
        QTAILQ_FOREACH(i, &fw_boot_order, link) {
            if (i->bootindex == bootindex) {
                error_setg(errp, "The bootindex %d has already been used",
                           bootindex);
                return;
            }
        }
    }
}

void del_boot_device_path(DeviceState *dev, const char *suffix)
{
    FWBootEntry *i;

    if (dev == NULL) {
        return;
    }

    QTAILQ_FOREACH(i, &fw_boot_order, link) {
        if ((!suffix || !g_strcmp0(i->suffix, suffix)) &&
             i->dev == dev) {
            QTAILQ_REMOVE(&fw_boot_order, i, link);
            g_free(i->suffix);
            g_free(i);

            break;
        }
    }
}

void add_boot_device_path(int32_t bootindex, DeviceState *dev,
                          const char *suffix)
{
    FWBootEntry *node, *i;

    if (bootindex < 0) {
        del_boot_device_path(dev, suffix);
        return;
    }

    assert(dev != NULL || suffix != NULL);

    del_boot_device_path(dev, suffix);

    node = g_new0(FWBootEntry, 1);
    node->bootindex = bootindex;
    node->suffix = g_strdup(suffix);
    node->dev = dev;

    QTAILQ_FOREACH(i, &fw_boot_order, link) {
        if (i->bootindex == bootindex) {
            error_report("Two devices with same boot index %d", bootindex);
            exit(1);
        } else if (i->bootindex < bootindex) {
            continue;
        }
        QTAILQ_INSERT_BEFORE(i, node, link);
        return;
    }
    QTAILQ_INSERT_TAIL(&fw_boot_order, node, link);
}

typedef struct {
    int32_t *bootindex;
    const char *suffix;
    DeviceState *dev;
} BootIndexProperty;

typedef struct FWLCHSEntry FWLCHSEntry;

struct FWLCHSEntry {
    QTAILQ_ENTRY(FWLCHSEntry) link;
    DeviceState *dev;
    char *suffix;
    uint32_t lcyls;
    uint32_t lheads;
    uint32_t lsecs;
};

static QTAILQ_HEAD(, FWLCHSEntry) fw_lchs =
    QTAILQ_HEAD_INITIALIZER(fw_lchs);

void add_boot_device_lchs(DeviceState *dev, const char *suffix,
                          uint32_t lcyls, uint32_t lheads, uint32_t lsecs)
{
    FWLCHSEntry *node;

    if (!lcyls && !lheads && !lsecs) {
        return;
    }

    assert(dev != NULL || suffix != NULL);

    node = g_new0(FWLCHSEntry, 1);
    node->suffix = g_strdup(suffix);
    node->dev = dev;
    node->lcyls = lcyls;
    node->lheads = lheads;
    node->lsecs = lsecs;

    QTAILQ_INSERT_TAIL(&fw_lchs, node, link);
}


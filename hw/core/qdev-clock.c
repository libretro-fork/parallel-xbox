/*
 * Device's clock input and output
 *
 * Copyright GreenSocs 2016-2020
 *
 * Authors:
 *  Frederic Konrad
 *  Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-core.h"
#include "qapi/error.h"

void qdev_finalize_clocklist(DeviceState *dev)
{
    /* called by @dev's device_finalize() */
    NamedClockList *ncl, *ncl_next;

    QLIST_FOREACH_SAFE(ncl, &dev->clocks, node, ncl_next) {
        QLIST_REMOVE(ncl, node);
        if (!ncl->alias) {
            /*
             * We kept a reference on the input clock to ensure it lives up to
             * this point; it is used by the monitor to show the frequency.
             */
            object_unref(OBJECT(ncl->clock));
        }
        g_free(ncl->name);
        g_free(ncl);
    }
}


/*
 *  Self-announce facility
 *  (c) 2017-2019 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_NET_ANNOUNCE_H
#define QEMU_NET_ANNOUNCE_H

#include "qapi/qapi-types-net.h"
#include "qemu/timer.h"

typedef struct AnnounceTimer {
    QEMUTimer *tm;
    AnnounceParameters params;
    QEMUClockType type;
    int round;
} AnnounceTimer;

/* Returns: update the timer to the next time point */

/*
 * Delete the underlying timer and other data
 * If 'free_named' true and the timer is a named timer, then remove
 * it from the list of named timers and free the AnnounceTimer itself.
 */

/*
 * Under BQL/main thread
 * Reset the timer to the given parameters/type/notifier.
 */

#endif

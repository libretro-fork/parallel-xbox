/*
 * Fsdev Throttle
 *
 * Copyright (C) 2016 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Pradeep Jagadeesh <pradeep.jagadeesh@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 *
 */

#ifndef QEMU_FSDEV_THROTTLE_H
#define QEMU_FSDEV_THROTTLE_H

#include "block/aio.h"
#include "qemu/throttle.h"

typedef struct FsThrottle {
    ThrottleState ts;
    ThrottleTimers tt;
    ThrottleConfig cfg;
} FsThrottle;


#endif /* QEMU_FSDEV_THROTTLE_H */

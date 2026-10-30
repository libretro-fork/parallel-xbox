/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef QEMU_COLO_H
#define QEMU_COLO_H

#include "qapi/qapi-types-migration.h"

enum colo_event {
    COLO_EVENT_NONE,
    COLO_EVENT_CHECKPOINT,
    COLO_EVENT_FAILOVER,
};

/* COLO (coarse-grained lock-stepping) needs two running QEMUs: never on */
static inline bool migration_in_colo_state(void)
{
    return false;
}

static inline bool migration_incoming_colo_enabled(void)
{
    return false;
}

static inline bool migration_incoming_in_colo_state(void)
{
    return false;
}

#endif

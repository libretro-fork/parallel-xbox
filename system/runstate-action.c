/*
 * Copyright (c) 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "system/runstate-action.h"
#include "system/watchdog.h"
#include "qemu/config-file.h"
#include "qapi/error.h"
#include "qemu/option_int.h"

RebootAction reboot_action = REBOOT_ACTION_RESET;
ShutdownAction shutdown_action = SHUTDOWN_ACTION_POWEROFF;
PanicAction panic_action = PANIC_ACTION_SHUTDOWN;


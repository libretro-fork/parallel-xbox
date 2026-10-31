/*
 * Semihosting Stubs for system emulation
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * Stubs for system targets that don't actually do semihosting.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "semihosting/semihost.h"

int semihosting_get_argc(void)
{
    return 0;
}

void semihosting_arg_fallback(const char *file, const char *cmd)
{
}

void qemu_semihosting_chardev_init(void)
{
}

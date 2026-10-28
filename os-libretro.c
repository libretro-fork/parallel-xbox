/*
 * Process setup for the libretro core: none.
 *
 * The core runs in the frontend's process, so the standalone's process
 * setup (os-posix.c, os-win32.c) is not its to do: no signal or console
 * Ctrl handlers (they took the frontend's, and Windows' slept 10 s in it),
 * no unbuffered stdout/stderr (the frontend's logging), no file descriptor
 * limit, timer resolution, daemon, chroot, run-as user or mlock. These
 * answer qemu_init the way a process that asked for none of that is
 * answered.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

void os_set_line_buffering(void)
{
}

void os_setup_early_signal_handling(void)
{
}

#ifndef _WIN32
void os_set_proc_name(const char *s)
{
}

void os_setup_signal_handling(void)
{
}

int os_set_daemonize(bool d)
{
    return d ? -ENOTSUP : 0;
}

bool is_daemonized(void)
{
    return false;
}

void os_daemonize(void)
{
}

bool os_set_runas(const char *user_id)
{
    return false;
}

void os_set_chroot(const char *path)
{
}

void os_setup_limits(void)
{
}

void os_setup_post(void)
{
}

int os_mlock(bool on_fault)
{
    return -ENOSYS;
}
#endif

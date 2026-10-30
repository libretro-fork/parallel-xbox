/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"

#if defined(__APPLE__) && defined(__aarch64__)
#include <memmap.h>

/* QEMU sets an absolute mode, including repeated write requests during
 * translation and execute requests without an intervening write. Adapt
 * that contract to libretro-common's balanced, nested write scopes.
 * One state shared by every caller on this thread (TCG and DSP included).
 */
static __thread bool jit_writable;

void qemu_thread_jit_write(void)
{
    if (!jit_writable) {
        memjit_write_begin();
        jit_writable = true;
    }
}

void qemu_thread_jit_execute(void)
{
    if (jit_writable) {
        memjit_write_end();
        jit_writable = false;
    }
}
#endif

/* Disabled QEMU debug logging contract for shared translation helpers.
 * Include qemu/log.h from callers. The core uses frontend logging and
 * error-report diagnostics instead of the standalone debug-file service.
 *
 * This code is licensed under the GNU General Public License,
 * version 2 or (at your option) any later version.
 */

#ifndef QEMU_LOG_FOR_TRACE_H
#define QEMU_LOG_FOR_TRACE_H

/* The libretro launcher never enables QEMU debug-file logging. Keep the
 * mask constant so translation helpers compile away their disabled paths. */
#define qemu_loglevel 0u

#define LOG_TRACE (1u << 15)

static inline bool qemu_loglevel_mask(int mask)
{
    (void)mask;
    return false;
}

/* Retain printf checking and argument evaluation for direct calls. */
static inline void G_GNUC_PRINTF(1, 2) qemu_log(const char *fmt, ...)
{
    (void)fmt;
}

#endif

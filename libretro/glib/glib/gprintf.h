/*
 * The in-tree GLib: <glib/gprintf.h>, the C library's printf family.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef LR_GLIB_GPRINTF_H
#define LR_GLIB_GPRINTF_H

#include <glib.h>
#include <stdio.h>
#include <stdarg.h>

#define g_printf printf
#define g_fprintf fprintf
#define g_sprintf sprintf
#define g_vprintf vprintf
#define g_vfprintf vfprintf
#define g_vsprintf vsprintf

#endif

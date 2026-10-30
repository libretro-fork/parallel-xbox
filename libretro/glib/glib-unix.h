/*
 * The in-tree GLib: <glib-unix.h>, what QEMU uses of it (gunix.c).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef LR_GLIB_UNIX_H
#define LR_GLIB_UNIX_H

#include <glib.h>

#ifndef _WIN32
#include <dirent.h>

gboolean g_unix_open_pipe(gint *fds, gint flags, GError **error);
gboolean g_unix_set_fd_nonblocking(gint fd, gboolean nonblock,
                                   GError **error);
#endif

#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_GWINPOLL_H
#define XEMU_GWINPOLL_H
#include "glib.h"
#ifdef _WIN32
/* An extra wake handle does not consume a GPollFD or count as a ready fd.
 * At MAXIMUM_WAIT_OBJECTS descriptors it is omitted; timeout still applies. */
gint g_poll_with_handle(GPollFD *fds, guint nfds, gint timeout, gintptr handle);
#endif
#endif

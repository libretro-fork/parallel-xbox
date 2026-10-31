/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_EVENT_INTERNAL_H
#define QEMU_EVENT_INTERNAL_H

/* Fork child only, before any replacement workers are started. */
void qemu_eventcount_after_fork(void);

#endif

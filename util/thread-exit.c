/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Derived from the QEMU thread backends.
 * Copyright Red Hat, Inc. 2009, 2010
 */
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/notify.h"

/* Owner-local callbacks. Managed rthreads drain on return; the host-owned
 * emulation thread drains explicitly before leaving the core. Detach each
 * record before invoking a callback that may free its enclosing storage. */
static __thread NotifierList thread_exit;

void qemu_thread_atexit_add(Notifier *notifier)
{
    notifier_list_add(&thread_exit, notifier);
}

void qemu_thread_atexit_remove(Notifier *notifier)
{
    notifier_remove(notifier);
}

void qemu_thread_run_atexit(void)
{
    Notifier *notifier;
    while ((notifier = QLIST_FIRST(&thread_exit.notifiers))) {
        notifier_remove(notifier);
        notifier->notify(notifier, NULL);
    }
}

/*
 * Postcopy migration for RAM
 *
 * Copyright 2013 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Dave Gilbert  <dgilbert@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef QEMU_POSTCOPY_RAM_H
#define QEMU_POSTCOPY_RAM_H

#include "qapi/qapi-types-migration.h"

/* Return true if the host supports everything we need to do postcopy-ram */

/*
 * Make all of RAM sensitive to accesses to areas that haven't yet been written
 * and wire up anything necessary to deal with it.
 */

/*
 * Initialise postcopy-ram, setting the RAM to a state where we can go into
 * postcopy later; must be called prior to any precopy.
 * called from ram.c's similarly named ram_postcopy_incoming_init
 */

/*
 * At the end of a migration where postcopy_ram_incoming_init was called.
 */

/*
 * Userfault requires us to mark RAM as NOHUGEPAGE prior to discard
 * however leaving it until after precopy means that most of the precopy
 * data is still THPd
 */

/*
 * Called at the start of each RAMBlock by the bitmap code.
 */

/*
 * Called by the bitmap code for each chunk to discard.
 * May send a discard message, may just leave it queued to
 * be sent later.
 * @start,@length: a range of pages in the migration bitmap in the
 *  RAM block passed to postcopy_discard_send_init() (length=1 is one page)
 */

/*
 * Called at the end of each RAMBlock by the bitmap code.
 * Sends any outstanding discard messages.
 */

/*
 * Place a page (from) at (host) efficiently
 *    There are restrictions on how 'from' must be mapped, in general best
 *    to use other postcopy_ routines to allocate.
 * returns 0 on success
 */

/*
 * Place a zero page at (host) atomically
 * returns 0 on success
 */

/*
 * The incoming postcopy state.  A savestate stream carries no postcopy
 * commands (loadvm_process_command() refuses every command), so a load
 * never leaves POSTCOPY_INCOMING_NONE.
 */
typedef enum {
    POSTCOPY_INCOMING_NONE = 0,  /* Initial state - no postcopy */
    POSTCOPY_INCOMING_ADVISE,
    POSTCOPY_INCOMING_DISCARD,
    POSTCOPY_INCOMING_LISTENING,
    POSTCOPY_INCOMING_RUNNING,
    POSTCOPY_INCOMING_END
} PostcopyState;

static inline PostcopyState postcopy_state_get(void)
{
    return POSTCOPY_INCOMING_NONE;
}

void postcopy_fault_thread_notify(MigrationIncomingState *mis);

/*
 * To be called once at the start before any device initialisation
 */

/* Add a notifier to a list to be called when checking whether the devices
 * can support postcopy.
 * It's data is a *PostcopyNotifyData
 * It should return 0 if OK, or a negative value on failure.
 * On failure it must set the data->errp to an error.
 *
 */
enum PostcopyNotifyReason {
    POSTCOPY_NOTIFY_PROBE = 0,
    POSTCOPY_NOTIFY_INBOUND_ADVISE,
    POSTCOPY_NOTIFY_INBOUND_LISTEN,
    POSTCOPY_NOTIFY_INBOUND_END,
};

struct PostcopyNotifyData {
    enum PostcopyNotifyReason reason;
};

/* Call the notifier list set by postcopy_add_start_notifier */

struct PostCopyFD;

/* ufd is a pointer to the struct uffd_msg *TODO: more Portable! */
typedef int (*pcfdhandler)(struct PostCopyFD *pcfd, void *ufd);
/* Notification to wake, either on place or on reception of
 * a fault on something that's already arrived (race)
 */
typedef int (*pcfdwake)(struct PostCopyFD *pcfd, RAMBlock *rb, uint64_t offset);

struct PostCopyFD {
    int fd;
    /* Data to pass to handler */
    void *data;
    /* Handler to be called whenever we get a poll event */
    pcfdhandler handler;
    /* Notification to wake shared client */
    pcfdwake waker;
    /* A string to use in error messages */
    const char *idstr;
};

/* Register a userfaultfd owned by an external process for
 * shared memory.
 */
/* Call each of the shared 'waker's registered telling them of
 * availability of a block.
 */
/* Callback from shared fault handlers to ask for a page */

/* Hard-code channels for now for postcopy preemption */
enum PostcopyChannels {
    RAM_CHANNEL_PRECOPY = 0,
    RAM_CHANNEL_POSTCOPY = 1,
    RAM_CHANNEL_MAX,
};

bool postcopy_is_paused(MigrationStatus status);


#endif

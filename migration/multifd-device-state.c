/*
 * Multifd device state migration
 *
 * Copyright (C) 2024,2025 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/lockable.h"
#include "block/thread-pool.h"
#include "migration.h"
#include "migration/misc.h"
#include "multifd.h"
#include "options.h"

static struct {
    QemuMutex queue_job_mutex;

    MultiFDSendData *send_data;

    ThreadPool *threads;
    retro_atomic_int_t threads_abort;
} *multifd_send_device_state;

void multifd_send_data_clear_device_state(MultiFDDeviceState_t *device_state)
{
    g_clear_pointer(&device_state->idstr, g_free);
    g_clear_pointer(&device_state->buf, g_free);
}

bool multifd_device_state_supported(void)
{
    return migrate_multifd() && !migrate_mapped_ram() &&
        migrate_multifd_compression() == MULTIFD_COMPRESSION_NONE;
}

static void multifd_device_state_save_thread_data_free(void *opaque)
{
    SaveCompletePrecopyThreadData *data = opaque;

    g_clear_pointer(&data->idstr, g_free);
    g_free(data);
}

static int multifd_device_state_save_thread(void *opaque)
{
    SaveCompletePrecopyThreadData *data = opaque;
    g_autoptr(Error) local_err = NULL;

    if (!data->hdlr(data, &local_err)) {
        MigrationState *s = migrate_get_current();

        /*
         * Can't call abort_device_state_save_threads() here since new
         * save threads could still be in process of being launched
         * (if, for example, the very first save thread launched exited
         * with an error very quickly).
         */

        assert(local_err);

        /*
         * In case of multiple save threads failing which thread error
         * return we end setting is purely arbitrary.
         */
        migrate_set_error(s, local_err);
    }

    return 0;
}

void
multifd_spawn_device_state_save_thread(SaveCompletePrecopyThreadHandler hdlr,
                                       char *idstr, uint32_t instance_id,
                                       void *opaque)
{
    SaveCompletePrecopyThreadData *data;

    assert(multifd_device_state_supported());
    assert(multifd_send_device_state);

    assert(!retro_atomic_load_relaxed_int(&multifd_send_device_state->threads_abort));

    data = g_new(SaveCompletePrecopyThreadData, 1);
    data->hdlr = hdlr;
    data->idstr = g_strdup(idstr);
    data->instance_id = instance_id;
    data->handler_opaque = opaque;

    thread_pool_submit_immediate(multifd_send_device_state->threads,
                                 multifd_device_state_save_thread,
                                 data,
                                 multifd_device_state_save_thread_data_free);
}

void multifd_abort_device_state_save_threads(void)
{
    assert(multifd_device_state_supported());

    retro_atomic_store_relaxed_int(&multifd_send_device_state->threads_abort, true);
}

bool multifd_join_device_state_save_threads(void)
{
    MigrationState *s = migrate_get_current();

    assert(multifd_device_state_supported());

    thread_pool_wait(multifd_send_device_state->threads);

    return !migrate_has_error(s);
}

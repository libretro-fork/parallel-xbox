/*
 * Copyright (c) 2021-2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/vfio/vfio-device.h"
#include "migration/cpr.h"
#include "migration/misc.h"
#include "migration/options.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "system/runstate.h"
#include "trace.h"

/*************************************************************************/
/* cpr state container for all information to be saved. */

CprState cpr_state;

/****************************************************************************/

typedef struct CprFd {
    char *name;
    unsigned int namelen;
    int id;
    int fd;
    QLIST_ENTRY(CprFd) next;
} CprFd;

void cpr_save_fd(const char *name, int id, int fd)
{
    CprFd *elem = g_new0(CprFd, 1);

    trace_cpr_save_fd(name, id, fd);
    elem->name = g_strdup(name);
    elem->namelen = strlen(name) + 1;
    elem->id = id;
    elem->fd = fd;
    QLIST_INSERT_HEAD(&cpr_state.fds, elem, next);
}

static CprFd *find_fd(CprFdList *head, const char *name, int id)
{
    CprFd *elem;

    QLIST_FOREACH(elem, head, next) {
        if (!strcmp(elem->name, name) && elem->id == id) {
            return elem;
        }
    }
    return NULL;
}

void cpr_delete_fd(const char *name, int id)
{
    CprFd *elem = find_fd(&cpr_state.fds, name, id);

    if (elem) {
        QLIST_REMOVE(elem, next);
        g_free(elem->name);
        g_free(elem);
    }

    trace_cpr_delete_fd(name, id);
}

int cpr_find_fd(const char *name, int id)
{
    CprFd *elem = find_fd(&cpr_state.fds, name, id);
    int fd = elem ? elem->fd : -1;

    trace_cpr_find_fd(name, id, fd);
    return fd;
}

bool cpr_walk_fd(cpr_walk_fd_cb cb)
{
    CprFd *elem;

    QLIST_FOREACH(elem, &cpr_state.fds, next) {
        g_assert(elem->fd >= 0);
        if (!cb(elem->fd)) {
            return false;
        }
    }
    return true;
}

/*************************************************************************/
/*************************************************************************/

static QEMUFile *cpr_state_file;

static MigMode incoming_mode = MIG_MODE_NONE;

void cpr_set_incoming_mode(MigMode mode)
{
    incoming_mode = mode;
}

bool cpr_is_incoming(void)
{
    return incoming_mode != MIG_MODE_NONE;
}

void cpr_state_close(void)
{
    if (cpr_state_file) {
        qemu_fclose(cpr_state_file);
        cpr_state_file = NULL;
    }
}


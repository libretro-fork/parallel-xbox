/*
 * Core Definitions for QAPI/QMP Dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Michael Roth      <mdroth@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/qmp-registry.h"

void qmp_register_command(QmpCommandList *cmds, const char *name,
                          QmpCommandFunc *fn, QmpCommandOptions options,
                          uint64_t features)
{
    QmpCommand *cmd = g_malloc0(sizeof(*cmd));

    /* QCO_COROUTINE and QCO_ALLOW_OOB are incompatible for now */
    assert(!((options & QCO_COROUTINE) && (options & QCO_ALLOW_OOB)));

    cmd->name = name;
    cmd->fn = fn;
    cmd->enabled = true;
    cmd->options = options;
    cmd->features = features;
    QTAILQ_INSERT_TAIL(cmds, cmd, node);
}


/*
 * Core Definitions for QAPI/QMP Dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QAPI_QMP_DISPATCH_H
#define QAPI_QMP_DISPATCH_H

#include "monitor/monitor.h"
#include "qemu/queue.h"

typedef void (QmpCommandFunc)(QDict *, QObject **, Error **);

typedef enum QmpCommandOptions
{
    QCO_NO_SUCCESS_RESP       =  (1U << 0),
    QCO_ALLOW_OOB             =  (1U << 1),
    QCO_ALLOW_PRECONFIG       =  (1U << 2),
    QCO_COROUTINE             =  (1U << 3),
} QmpCommandOptions;

typedef struct QmpCommand
{
    const char *name;
    /* Runs in coroutine context if QCO_COROUTINE is set */
    QmpCommandFunc *fn;
    QmpCommandOptions options;
    uint64_t features;
    QTAILQ_ENTRY(QmpCommand) node;
    bool enabled;
    const char *disable_reason;
} QmpCommand;

typedef QTAILQ_HEAD(QmpCommandList, QmpCommand) QmpCommandList;

void qmp_register_command(QmpCommandList *cmds, const char *name,
                          QmpCommandFunc *fn, QmpCommandOptions options,
                          uint64_t features);

bool qmp_command_available(const QmpCommand *cmd, Error **errp);

typedef void (*qmp_cmd_callback_fn)(const QmpCommand *cmd, void *opaque);

#endif

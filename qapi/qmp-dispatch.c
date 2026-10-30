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

#include "qemu/osdep.h"

#include "block/aio.h"
#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qapi/qmp-registry.h"
#include "qobject/qdict.h"
#include "qobject/qjson.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qobject/qbool.h"
#include "qemu/coroutine.h"
#include "qemu/main-loop.h"

Visitor *qobject_input_visitor_new_qmp(QObject *obj)
{
    Visitor *v = qobject_input_visitor_new(obj);

    visit_set_policy(v, &compat_policy);
    return v;
}

Visitor *qobject_output_visitor_new_qmp(QObject **result)
{
    Visitor *v = qobject_output_visitor_new(result);

    visit_set_policy(v, &compat_policy);
    return v;
}

typedef struct QmpDispatchBH {
    const QmpCommand *cmd;
    Monitor *cur_mon;
    QDict *args;
    QObject **ret;
    Error **errp;
    Coroutine *co;
} QmpDispatchBH;


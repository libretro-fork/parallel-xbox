/*
 * QObject
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qobject/qbool.h"
#include "qobject/qnull.h"
#include "qobject/qnum.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qstring.h"
#include "qobject-internal.h"

QEMU_BUILD_BUG_MSG(
    offsetof(QNull, base) != 0 ||
    offsetof(QNum, base) != 0 ||
    offsetof(QString, base) != 0 ||
    offsetof(QDict, base) != 0 ||
    offsetof(QList, base) != 0 ||
    offsetof(QBool, base) != 0,
    "base qobject must be at offset 0");

static void (*qdestroy[QTYPE__MAX])(QObject *) = {
    [QTYPE_NONE] = NULL,               /* No such object exists */
    [QTYPE_QNULL] = NULL,              /* qnull_ is indestructible */
    [QTYPE_QNUM] = qnum_destroy_obj,
    [QTYPE_QSTRING] = qstring_destroy_obj,
    [QTYPE_QDICT] = qdict_destroy_obj,
    [QTYPE_QLIST] = qlist_destroy_obj,
    [QTYPE_QBOOL] = qbool_destroy_obj,
};

void qobject_destroy(QObject *obj)
{
    assert(!retro_atomic_load_relaxed_size(&obj->base.refcnt));
    assert(QTYPE_QNULL < obj->base.type && obj->base.type < QTYPE__MAX);
    qdestroy[obj->base.type](obj);
}



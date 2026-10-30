/*
 * QList Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qobject/qbool.h"
#include "qobject/qlist.h"
#include "qobject/qnull.h"
#include "qobject/qnum.h"
#include "qobject/qstring.h"
#include "qemu/queue.h"
#include "qobject-internal.h"

/**
 * qlist_new(): Create a new QList
 *
 * Return strong reference.
 */
QList *qlist_new(void)
{
    QList *qlist;

    qlist = g_malloc(sizeof(*qlist));
    qobject_init(QOBJECT(qlist), QTYPE_QLIST);
    QTAILQ_INIT(&qlist->head);

    return qlist;
}

/**
 * qlist_append_obj(): Append an QObject into QList
 *
 * NOTE: ownership of 'value' is transferred to the QList
 */
void qlist_append_obj(QList *qlist, QObject *value)
{
    QListEntry *entry;

    entry = g_malloc(sizeof(*entry));
    entry->value = value;

    QTAILQ_INSERT_TAIL(&qlist->head, entry, next);
}

int qlist_empty(const QList *qlist)
{
    return QTAILQ_EMPTY(&qlist->head);
}

/**
 * qlist_destroy_obj(): Free all the memory allocated by a QList
 */
void qlist_destroy_obj(QObject *obj)
{
    QList *qlist;
    QListEntry *entry, *next_entry;

    assert(obj != NULL);
    qlist = qobject_to(QList, obj);

    QTAILQ_FOREACH_SAFE(entry, &qlist->head, next, next_entry) {
        QTAILQ_REMOVE(&qlist->head, entry, next);
        qobject_unref(entry->value);
        g_free(entry);
    }

    g_free(qlist);
}

void qlist_unref(QList *q)
{
    qobject_unref(q);
}

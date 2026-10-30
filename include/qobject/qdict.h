/*
 * QDict Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef QDICT_H
#define QDICT_H

#include "qobject/qobject.h"
#include "qemu/queue.h"

#define QDICT_BUCKET_MAX 512

typedef struct QDictEntry {
    char *key;
    QObject *value;
    QLIST_ENTRY(QDictEntry) next;
} QDictEntry;

struct QDict {
    struct QObjectBase_ base;
    size_t size;
    QLIST_HEAD(,QDictEntry) table[QDICT_BUCKET_MAX];
};

void qdict_unref(QDict *q);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QDict, qdict_unref)

/* Object API */
QDict *qdict_new(void);
const char *qdict_entry_key(const QDictEntry *entry);
QObject *qdict_entry_value(const QDictEntry *entry);
size_t qdict_size(const QDict *qdict);
void qdict_put_obj(QDict *qdict, const char *key, QObject *value);
void qdict_del(QDict *qdict, const char *key);
int qdict_haskey(const QDict *qdict, const char *key);
QObject *qdict_get(const QDict *qdict, const char *key);
const QDictEntry *qdict_first(const QDict *qdict);
const QDictEntry *qdict_next(const QDict *qdict, const QDictEntry *entry);

/* Helper to qdict_put_obj(), accepts any object */
#define qdict_put(qdict, key, obj) \
        qdict_put_obj(qdict, key, QOBJECT(obj))

void qdict_put_int(QDict *qdict, const char *key, int64_t value);
void qdict_put_str(QDict *qdict, const char *key, const char *value);

const char *qdict_get_try_str(const QDict *qdict, const char *key);

QDict *qdict_clone_shallow(const QDict *src);

#endif /* QDICT_H */

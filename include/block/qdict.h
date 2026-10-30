/*
 * Special QDict functions used by the block layer
 *
 * Copyright (c) 2013-2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_QDICT_H
#define BLOCK_QDICT_H

#include "qobject/qdict.h"

QObject *qdict_crumple(const QDict *src, Error **errp);


void qdict_extract_subqdict(QDict *src, QDict **dst, const char *start);

typedef struct QDictRenames {
    const char *from;
    const char *to;
} QDictRenames;

#endif

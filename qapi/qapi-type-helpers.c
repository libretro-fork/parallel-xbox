/*
 * QAPI common helper functions
 *
 * This file provides helper functions related to types defined
 * in the QAPI schema.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"

char **strv_from_str_list(const strList *list)
{
    const strList *tail;
    int i = 0;
    char **strv = g_new(char *, QAPI_LIST_LENGTH(list) + 1);

    for (tail = list; tail != NULL; tail = tail->next) {
        strv[i++] = g_strdup(tail->value);
    }
    strv[i] = NULL;

    return strv;
}

/*
 * Special QDict functions used by the block layer
 *
 * Copyright (c) 2013-2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "block/qdict.h"
#include "qobject/qbool.h"
#include "qobject/qlist.h"
#include "qobject/qnum.h"
#include "qobject/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/cutils.h"
#include "qapi/error.h"


/**
 * qdict_split_flat_key:
 * @key: the key string to split
 * @prefix: non-NULL pointer to hold extracted prefix
 * @suffix: non-NULL pointer to remaining suffix
 *
 * Given a flattened key such as 'foo.0.bar', split it into two parts
 * at the first '.' separator. Allows double dot ('..') to escape the
 * normal separator.
 *
 * e.g.
 *    'foo.0.bar' -> prefix='foo' and suffix='0.bar'
 *    'foo..0.bar' -> prefix='foo.0' and suffix='bar'
 *
 * The '..' sequence will be unescaped in the returned 'prefix'
 * string. The 'suffix' string will be left in escaped format, so it
 * can be fed back into the qdict_split_flat_key() key as the input
 * later.
 *
 * The caller is responsible for freeing the string returned in @prefix
 * using g_free().
 */
static void qdict_split_flat_key(const char *key, char **prefix,
                                 const char **suffix)
{
    const char *separator;
    size_t i, j;

    /* Find first '.' separator, but if there is a pair '..'
     * that acts as an escape, so skip over '..' */
    separator = NULL;
    do {
        if (separator) {
            separator += 2;
        } else {
            separator = key;
        }
        separator = strchr(separator, '.');
    } while (separator && separator[1] == '.');

    if (separator) {
        *prefix = g_strndup(key, separator - key);
        *suffix = separator + 1;
    } else {
        *prefix = g_strdup(key);
        *suffix = NULL;
    }

    /* Unescape the '..' sequence into '.' */
    for (i = 0, j = 0; (*prefix)[i] != '\0'; i++, j++) {
        if ((*prefix)[i] == '.') {
            assert((*prefix)[i + 1] == '.');
            i++;
        }
        (*prefix)[j] = (*prefix)[i];
    }
    (*prefix)[j] = '\0';
}

/**
 * qdict_is_list:
 * @maybe_list: dict to check if keys represent list elements.
 *
 * Determine whether all keys in @maybe_list are valid list elements.
 * If @maybe_list is non-zero in length and all the keys look like
 * valid list indexes, this will return 1. If @maybe_list is zero
 * length or all keys are non-numeric then it will return 0 to indicate
 * it is a normal qdict. If there is a mix of numeric and non-numeric
 * keys, or the list indexes are non-contiguous, an error is reported.
 *
 * Returns: 1 if a valid list, 0 if a dict, -1 on error
 */
static int qdict_is_list(QDict *maybe_list, Error **errp)
{
    const QDictEntry *ent;
    ssize_t len = 0;
    ssize_t max = -1;
    int is_list = -1;
    int64_t val;

    for (ent = qdict_first(maybe_list); ent != NULL;
         ent = qdict_next(maybe_list, ent)) {
        int is_index = !qemu_strtoi64(ent->key, NULL, 10, &val);

        if (is_list == -1) {
            is_list = is_index;
        }

        if (is_index != is_list) {
            error_setg(errp, "Cannot mix list and non-list keys");
            return -1;
        }

        if (is_index) {
            len++;
            if (val > max) {
                max = val;
            }
        }
    }

    if (is_list == -1) {
        assert(!qdict_size(maybe_list));
        is_list = 0;
    }

    /* NB this isn't a perfect check - e.g. it won't catch
     * a list containing '1', '+1', '01', '3', but that
     * does not matter - we've still proved that the
     * input is a list. It is up the caller to do a
     * stricter check if desired */
    if (len != (max + 1)) {
        error_setg(errp, "List indices are not contiguous, "
                   "saw %zd elements but %zd largest index",
                   len, max);
        return -1;
    }

    return is_list;
}

/**
 * qdict_crumple:
 * @src: the original flat dictionary (only scalar values) to crumple
 *
 * Takes a flat dictionary whose keys use '.' separator to indicate
 * nesting, and values are scalars, empty dictionaries or empty lists,
 * and crumples it into a nested structure.
 *
 * To include a literal '.' in a key name, it must be escaped as '..'
 *
 * For example, an input of:
 *
 * { 'foo.0.bar': 'one', 'foo.0.wizz': '1',
 *   'foo.1.bar': 'two', 'foo.1.wizz': '2' }
 *
 * will result in an output of:
 *
 * {
 *   'foo': [
 *      { 'bar': 'one', 'wizz': '1' },
 *      { 'bar': 'two', 'wizz': '2' }
 *   ],
 * }
 *
 * The following scenarios in the input dict will result in an
 * error being returned:
 *
 *  - Any values in @src are non-scalar types
 *  - If keys in @src imply that a particular level is both a
 *    list and a dict. e.g., "foo.0.bar" and "foo.eek.bar".
 *  - If keys in @src imply that a particular level is a list,
 *    but the indices are non-contiguous. e.g. "foo.0.bar" and
 *    "foo.2.bar" without any "foo.1.bar" present.
 *  - If keys in @src represent list indexes, but are not in
 *    the "%zu" format. e.g. "foo.+0.bar"
 *
 * Returns: either a QDict or QList for the nested data structure, or NULL
 * on error
 */
QObject *qdict_crumple(const QDict *src, Error **errp)
{
    const QDictEntry *ent;
    QDict *two_level, *multi_level = NULL, *child_dict;
    QDict *dict_val;
    QList *list_val;
    QObject *dst = NULL, *child;
    size_t i;
    char *prefix = NULL;
    const char *suffix = NULL;
    int is_list;

    two_level = qdict_new();

    /* Step 1: split our totally flat dict into a two level dict */
    for (ent = qdict_first(src); ent != NULL; ent = qdict_next(src, ent)) {
        dict_val = qobject_to(QDict, ent->value);
        list_val = qobject_to(QList, ent->value);
        if ((dict_val && qdict_size(dict_val))
            || (list_val && !qlist_empty(list_val))) {
            error_setg(errp, "Value %s is not flat", ent->key);
            goto error;
        }

        qdict_split_flat_key(ent->key, &prefix, &suffix);
        child = qdict_get(two_level, prefix);
        child_dict = qobject_to(QDict, child);

        if (child) {
            /*
             * If @child_dict, then all previous keys with this prefix
             * had a suffix.  If @suffix, this one has one as well,
             * and we're good, else there's a clash.
             */
            if (!child_dict || !suffix) {
                error_setg(errp, "Cannot mix scalar and non-scalar keys");
                goto error;
            }
        }

        if (suffix) {
            if (!child_dict) {
                child_dict = qdict_new();
                qdict_put(two_level, prefix, child_dict);
            }
            qdict_put_obj(child_dict, suffix, qobject_ref(ent->value));
        } else {
            qdict_put_obj(two_level, prefix, qobject_ref(ent->value));
        }

        g_free(prefix);
        prefix = NULL;
    }

    /* Step 2: optionally process the two level dict recursively
     * into a multi-level dict */
    multi_level = qdict_new();
    for (ent = qdict_first(two_level); ent != NULL;
         ent = qdict_next(two_level, ent)) {
        dict_val = qobject_to(QDict, ent->value);
        if (dict_val && qdict_size(dict_val)) {
            child = qdict_crumple(dict_val, errp);
            if (!child) {
                goto error;
            }

            qdict_put_obj(multi_level, ent->key, child);
        } else {
            qdict_put_obj(multi_level, ent->key, qobject_ref(ent->value));
        }
    }
    qobject_unref(two_level);
    two_level = NULL;

    /* Step 3: detect if we need to turn our dict into list */
    is_list = qdict_is_list(multi_level, errp);
    if (is_list < 0) {
        goto error;
    }

    if (is_list) {
        dst = QOBJECT(qlist_new());

        for (i = 0; i < qdict_size(multi_level); i++) {
            char *key = g_strdup_printf("%zu", i);

            child = qdict_get(multi_level, key);
            g_free(key);

            if (!child) {
                error_setg(errp, "Missing list index %zu", i);
                goto error;
            }

            qlist_append_obj(qobject_to(QList, dst), qobject_ref(child));
        }
        qobject_unref(multi_level);
        multi_level = NULL;
    } else {
        dst = QOBJECT(multi_level);
    }

    return dst;

 error:
    g_free(prefix);
    qobject_unref(multi_level);
    qobject_unref(two_level);
    qobject_unref(dst);
    return NULL;
}


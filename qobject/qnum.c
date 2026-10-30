/*
 * QNum Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qobject/qnum.h"
#include "qobject-internal.h"

/**
 * qnum_from_int(): Create a new QNum from an int64_t
 *
 * Return strong reference.
 */
QNum *qnum_from_int(int64_t value)
{
    QNum *qn = g_new(QNum, 1);

    qobject_init(QOBJECT(qn), QTYPE_QNUM);
    qn->kind = QNUM_I64;
    qn->u.i64 = value;

    return qn;
}

/**
 * qnum_from_uint(): Create a new QNum from an uint64_t
 *
 * Return strong reference.
 */
QNum *qnum_from_uint(uint64_t value)
{
    QNum *qn = g_new(QNum, 1);

    qobject_init(QOBJECT(qn), QTYPE_QNUM);
    qn->kind = QNUM_U64;
    qn->u.u64 = value;

    return qn;
}

/**
 * qnum_from_double(): Create a new QNum from a double
 *
 * Return strong reference.
 */
QNum *qnum_from_double(double value)
{
    QNum *qn = g_new(QNum, 1);

    qobject_init(QOBJECT(qn), QTYPE_QNUM);
    qn->kind = QNUM_DOUBLE;
    qn->u.dbl = value;

    return qn;
}

/**
 * qnum_get_try_int(): Get an integer representation of the number
 *
 * Return true on success.
 */
bool qnum_get_try_int(const QNum *qn, int64_t *val)
{
    switch (qn->kind) {
    case QNUM_I64:
        *val = qn->u.i64;
        return true;
    case QNUM_U64:
        if (qn->u.u64 > INT64_MAX) {
            return false;
        }
        *val = qn->u.u64;
        return true;
    case QNUM_DOUBLE:
        return false;
    }

    g_assert_not_reached();
}

/**
 * qnum_get_uint(): Get an unsigned integer from the number
 *
 * Return true on success.
 */
bool qnum_get_try_uint(const QNum *qn, uint64_t *val)
{
    switch (qn->kind) {
    case QNUM_I64:
        if (qn->u.i64 < 0) {
            return false;
        }
        *val = qn->u.i64;
        return true;
    case QNUM_U64:
        *val = qn->u.u64;
        return true;
    case QNUM_DOUBLE:
        return false;
    }

    g_assert_not_reached();
}

/**
 * qnum_get_double(): Get a float representation of the number
 *
 * qnum_get_double() loses precision for integers beyond 53 bits.
 */
double qnum_get_double(QNum *qn)
{
    switch (qn->kind) {
    case QNUM_I64:
        return qn->u.i64;
    case QNUM_U64:
        return qn->u.u64;
    case QNUM_DOUBLE:
        return qn->u.dbl;
    }

    g_assert_not_reached();
}

char *qnum_to_string(QNum *qn)
{
    switch (qn->kind) {
    case QNUM_I64:
        return g_strdup_printf("%" PRId64, qn->u.i64);
    case QNUM_U64:
        return g_strdup_printf("%" PRIu64, qn->u.u64);
    case QNUM_DOUBLE:
        /* 17 digits suffice for IEEE double */
        return g_strdup_printf("%.17g", qn->u.dbl);
    }

    g_assert_not_reached();
}

/**
 * qnum_destroy_obj(): Free all memory allocated by a
 * QNum object
 */
void qnum_destroy_obj(QObject *obj)
{
    assert(obj != NULL);
    g_free(qobject_to(QNum, obj));
}

void qnum_unref(QNum *q)
{
    qobject_unref(q);
}

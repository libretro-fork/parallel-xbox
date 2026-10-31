/*
 * QObject JSON integration
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qobject/json-parser.h"
#include "qobject/json-writer.h"
#include "qobject/qjson.h"
#include "qobject/qbool.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qnum.h"
#include "qobject/qstring.h"

typedef struct JSONParsingState {
    JSONMessageParser parser;
    QObject *result;
    Error *err;
} JSONParsingState;

static void consume_json(void *opaque, QObject *json, Error *err)
{
    JSONParsingState *s = opaque;

    assert(!json != !err);
    assert(!s->result || !s->err);

    if (s->result) {
        qobject_unref(s->result);
        s->result = NULL;
        error_setg(&s->err, "Expecting at most one JSON value");
    }
    if (s->err) {
        qobject_unref(json);
        error_free(err);
        return;
    }
    s->result = json;
    s->err = err;
}

/*
 * Parse @string as JSON value.
 * If @ap is non-null, interpolate %-escapes.
 * Takes ownership of %p arguments.
 * On success, return the JSON value.
 * On failure, store an error through @errp and return NULL.
 * Ownership of %p arguments becomes indeterminate then.  To avoid
 * leaks, callers passing %p must terminate on error, e.g. by passing
 * &error_abort.
 */
static QObject *qobject_from_jsonv(const char *string, va_list *ap,
                                   Error **errp)
{
    JSONParsingState state = {};

    json_message_parser_init(&state.parser, consume_json, &state, ap);
    json_message_parser_feed(&state.parser, string, strlen(string));
    json_message_parser_flush(&state.parser);
    json_message_parser_destroy(&state.parser);

    if (!state.result && !state.err) {
        error_setg(&state.err, "Expecting a JSON value");
    }

    error_propagate(errp, state.err);
    return state.result;
}

/*
 * Parse @string as JSON value with %-escapes interpolated.
 * Abort on error.  Do not use with untrusted @string.
 * Return the resulting QObject.  It is never null.
 */
QObject *qobject_from_vjsonf_nofail(const char *string, va_list ap)
{
    va_list ap_copy;
    QObject *obj;

    /* va_copy() is needed when va_list is an array type */
    va_copy(ap_copy, ap);
    obj = qobject_from_jsonv(string, &ap_copy, &error_abort);
    va_end(ap_copy);

    assert(obj);
    return obj;
}

/*
 * Parse @string as JSON object with %-escapes interpolated.
 * Abort on error.  Do not use with untrusted @string.
 * Return the resulting QDict.  It is never null.
 */
QDict *qdict_from_vjsonf_nofail(const char *string, va_list ap)
{
    QDict *qdict;

    qdict = qobject_to(QDict, qobject_from_vjsonf_nofail(string, ap));
    assert(qdict);
    return qdict;
}

/*
 * Parse @string as JSON object with %-escapes interpolated.
 * Abort on error.  Do not use with untrusted @string.
 * Return the resulting QDict.  It is never null.
 */
QDict *qdict_from_jsonf_nofail(const char *string, ...)
{
    QDict *qdict;
    va_list ap;

    va_start(ap, string);
    qdict = qdict_from_vjsonf_nofail(string, ap);
    va_end(ap);
    return qdict;
}


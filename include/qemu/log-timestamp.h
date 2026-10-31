/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_LOG_TIMESTAMP_H
#define QEMU_LOG_TIMESTAMP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* Logging needs one UTC representation, not a heap-allocated date object. */
static inline bool qemu_format_log_timestamp(char *out, size_t size,
                                             int64_t usec)
{
    int64_t seconds = usec / 1000000;
    int fraction = usec % 1000000;
    struct tm utc;
    char date[32];
    int length;

    if (fraction < 0) {
        seconds--;
        fraction += 1000000;
    }
    time_t value = (time_t)seconds;
    if ((int64_t)value != seconds || !size) {
        return false;
    }
    out[0] = 0;
#ifdef _WIN32
    if (gmtime_s(&utc, &value)) {
        return false;
    }
#else
    if (!gmtime_r(&value, &utc)) {
        return false;
    }
#endif
    if (!strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc)) {
        return false;
    }
    length = snprintf(out, size, "%s.%06dZ", date, fraction);
    if (length < 0 || (size_t)length >= size) {
        out[0] = 0;
        return false;
    }
    return true;
}

#endif

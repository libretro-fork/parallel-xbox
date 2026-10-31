/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/log-timestamp.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    char buffer[40];
    const struct {
        int64_t usec;
        const char *expected;
    } cases[] = {
        { 0, "1970-01-01T00:00:00.000000Z" },
        { 999999, "1970-01-01T00:00:00.999999Z" },
        { 1000001, "1970-01-01T00:00:01.000001Z" },
        { INT64_C(951825600123456), "2000-02-29T12:00:00.123456Z" },
        { INT64_C(2147483648000000), "2038-01-19T03:14:08.000000Z" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        assert(qemu_format_log_timestamp(buffer, sizeof(buffer), cases[i].usec));
        assert(!strcmp(buffer, cases[i].expected));
    }
#ifndef _WIN32
    assert(qemu_format_log_timestamp(buffer, sizeof(buffer), -1));
    assert(!strcmp(buffer, "1969-12-31T23:59:59.999999Z"));
#endif
    assert(!qemu_format_log_timestamp(buffer, 1, 0));
    assert(!buffer[0]);
    buffer[0] = 'x';
    assert(!qemu_format_log_timestamp(buffer, 0, 0));
    assert(buffer[0] == 'x');
    puts("log timestamp: every check passed");
    return 0;
}

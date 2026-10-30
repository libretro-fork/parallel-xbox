/*
* Helper to hexdump a buffer
 *
 * Copyright (c) 2013 Red Hat, Inc.
 * Copyright (c) 2013 Gerd Hoffmann <kraxel@redhat.com>
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 * Copyright (c) 2013 Xilinx, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/host-utils.h"

static inline char hexdump_nibble(unsigned x)
{
    return (x < 10 ? '0' : 'a' - 10) + x;
}

static size_t hexdump_line_length(size_t buf_len, size_t unit_len,
                                  size_t block_len)
{
    size_t est = buf_len * 2;
    if (unit_len) {
        est += buf_len / unit_len;
    }
    if (block_len) {
        est += buf_len / block_len;
    }
    return est;
}

GString *qemu_hexdump_line(GString *str, const void *vbuf, size_t len,
                           size_t unit_len, size_t block_len)
{
    const uint8_t *buf = vbuf;
    size_t u, b;

    if (str == NULL) {
        /* Estimate the length of the output to avoid reallocs. */
        str = g_string_sized_new(hexdump_line_length(len, unit_len, block_len)
                                 + 1);
    }

    for (u = 0, b = 0; len; u++, b++, len--, buf++) {
        uint8_t c;

        if (unit_len && u == unit_len) {
            g_string_append_c(str, ' ');
            u = 0;
        }
        if (block_len && b == block_len) {
            g_string_append_c(str, ' ');
            b = 0;
        }

        c = *buf;
        g_string_append_c(str, hexdump_nibble(c / 16));
        g_string_append_c(str, hexdump_nibble(c % 16));
    }

    return str;
}

#define QEMU_HEXDUMP_LINE_BYTES 16
#define QEMU_HEXDUMP_UNIT 1
#define QEMU_HEXDUMP_BLOCK 4


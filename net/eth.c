/*
 * QEMU network structures definitions and helper functions
 *
 * Copyright (c) 2012 Ravello Systems LTD (http://ravellosystems.com)
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Tamir Shomer <tamirs@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "net/eth.h"
#include "net/checksum.h"
#include "net/tap.h"

bool eth_pad_short_frame(uint8_t *padded_pkt, size_t *padded_buflen,
                         const void *pkt, size_t pkt_size)
{
    assert(padded_buflen && *padded_buflen >= ETH_ZLEN);

    if (pkt_size >= ETH_ZLEN) {
        return false;
    }

    /* pad to minimum Ethernet frame length */
    memcpy(padded_pkt, pkt, pkt_size);
    memset(&padded_pkt[pkt_size], 0, ETH_ZLEN - pkt_size);
    *padded_buflen = ETH_ZLEN;

    return true;
}

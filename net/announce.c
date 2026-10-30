/*
 *  Self-announce
 *  (c) 2017-2019 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "net/announce.h"
#include "net/net.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-net.h"
#include "qapi/qapi-commands-net.h"
#include "trace.h"

#ifndef ETH_P_RARP
#define ETH_P_RARP 0x8035
#endif
#define ARP_HTYPE_ETH 0x0001
#define ARP_PTYPE_IP 0x0800
#define ARP_OP_REQUEST_REV 0x3


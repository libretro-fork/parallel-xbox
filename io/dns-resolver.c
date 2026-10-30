/*
 * QEMU DNS resolver
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "io/dns-resolver.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-sockets.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/module.h"

#ifndef AI_NUMERICSERV
# define AI_NUMERICSERV 0
#endif




struct QIODNSResolverLookupData {
    SocketAddress *addr;
    SocketAddress **addrs;
    size_t naddrs;
};






static const TypeInfo qio_dns_resolver_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QIO_DNS_RESOLVER,
    .instance_size = sizeof(QIODNSResolver),
};


static void qio_dns_resolver_register_types(void)
{
    type_register_static(&qio_dns_resolver_info);
}


type_init(qio_dns_resolver_register_types);

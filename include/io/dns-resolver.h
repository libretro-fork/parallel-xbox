/*
 * QEMU DNS resolver
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
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

#ifndef QIO_DNS_RESOLVER_H
#define QIO_DNS_RESOLVER_H

#include "qapi/qapi-types-sockets.h"
#include "qom/object.h"
#include "io/task.h"

#define TYPE_QIO_DNS_RESOLVER "qio-dns-resolver"
OBJECT_DECLARE_SIMPLE_TYPE(QIODNSResolver,
                           QIO_DNS_RESOLVER)


/**
 * QIODNSResolver:
 *
 * The QIODNSResolver class provides a framework for doing
 * DNS resolution on SocketAddress objects, independently
 * of socket creation.
 *
 * <example>
 *   <title>Resolving addresses synchronously</title>
 *   <programlisting>
 *    int mylisten(SocketAddress *addr, Error **errp) {
 *      QIODNSResolver *resolver = qio_dns_resolver_get_instance();
 *      SocketAddress **rawaddrs = NULL;
 *      size_t nrawaddrs = 0;
 *      Error *err = NULL;
 *      QIOChannel **socks = NULL;
 *      size_t nsocks = 0;
 *
 *      if (qio_dns_resolver_lookup_sync(dns, addr, &nrawaddrs,
 *                                       &rawaddrs, errp) < 0) {
 *          return -1;
 *      }
 *
 *      for (i = 0; i < nrawaddrs; i++) {
 *         QIOChannel *sock = qio_channel_new();
 *         Error *local_err = NULL;
 *         qio_channel_listen_sync(sock, rawaddrs[i], &local_err);
 *         if (local_err) {
 *            error_propagate(&err, local_err);
 *         } else {
 *            socks = g_renew(QIOChannelSocket *, socks, nsocks + 1);
 *            socks[nsocks++] = sock;
 *         }
 *         qapi_free_SocketAddress(rawaddrs[i]);
 *      }
 *      g_free(rawaddrs);
 *
 *      if (nsocks == 0) {
 *         error_propagate(errp, err);
 *      } else {
 *         error_free(err);
 *      }
 *   }
 *   </programlisting>
 * </example>
 *
 * <example>
 *   <title>Resolving addresses asynchronously</title>
 *   <programlisting>
 *    typedef struct MyListenData {
 *       Error *err;
 *       QIOChannelSocket **socks;
 *       size_t nsocks;
 *    } MyListenData;
 *
 *    void mylistenresult(QIOTask *task, void *opaque) {
 *      MyListenData *data = opaque;
 *      QIODNSResolver *resolver =
 *         QIO_DNS_RESOLVER(qio_task_get_source(task);
 *      SocketAddress **rawaddrs = NULL;
 *      size_t nrawaddrs = 0;
 *      Error *err = NULL;
 *
 *      if (qio_task_propagate_error(task, &data->err)) {
 *         return;
 *      }
 *
 *      qio_dns_resolver_lookup_result(resolver, task,
 *                                     &nrawaddrs, &rawaddrs);
 *
 *      for (i = 0; i < nrawaddrs; i++) {
 *         QIOChannel *sock = qio_channel_new();
 *         Error *local_err = NULL;
 *         qio_channel_listen_sync(sock, rawaddrs[i], &local_err);
 *         if (local_err) {
 *            error_propagate(&err, local_err);
 *         } else {
 *            socks = g_renew(QIOChannelSocket *, socks, nsocks + 1);
 *            socks[nsocks++] = sock;
 *         }
 *         qapi_free_SocketAddress(rawaddrs[i]);
 *      }
 *      g_free(rawaddrs);
 *
 *      if (nsocks == 0) {
 *         error_propagate(&data->err, err);
 *      } else {
 *         error_free(err);
 *      }
 *    }
 *
 *    void mylisten(SocketAddress *addr, MyListenData *data) {
 *      QIODNSResolver *resolver = qio_dns_resolver_get_instance();
 *      qio_dns_resolver_lookup_async(dns, addr,
 *                                    mylistenresult, data, NULL);
 *    }
 *   </programlisting>
 * </example>
 */
struct QIODNSResolver {
    Object parent;
};



#endif /* QIO_DNS_RESOLVER_H */

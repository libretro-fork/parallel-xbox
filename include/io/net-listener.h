/*
 * QEMU network listener
 *
 * Copyright (c) 2016-2017 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QIO_NET_LISTENER_H
#define QIO_NET_LISTENER_H

#include "io/channel-socket.h"
#include "qom/object.h"

#define TYPE_QIO_NET_LISTENER "qio-net-listener"
OBJECT_DECLARE_SIMPLE_TYPE(QIONetListener,
                           QIO_NET_LISTENER)

typedef struct QIONetListenerSource QIONetListenerSource;

typedef void (*QIONetListenerClientFunc)(QIONetListener *listener,
                                         QIOChannelSocket *sioc,
                                         gpointer data);

/**
 * QIONetListener:
 *
 * The QIONetListener object encapsulates the management of a
 * listening socket. It is able to listen on multiple sockets
 * concurrently, to deal with the scenario where IPv4 / IPv6
 * needs separate sockets, or there is a need to listen on a
 * subset of interface IP addresses, instead of the wildcard
 * address.
 */
struct QIONetListener {
    Object parent;

    char *name;
    QIONetListenerSource **source;
    size_t nsioc;
    /* At most one of context or aio_context will be set */
    GMainContext *context;
    AioContext *aio_context;

    bool connected;

    QemuMutex lock; /* Protects remaining fields */
    QIONetListenerClientFunc io_func;
    gpointer io_data;
    GDestroyNotify io_notify;
};





/**
 * qio_net_listener_disconnect:
 * @listener: the network listener object
 *
 * Disconnect the listener, removing all I/O callback
 * watches and closing the socket channels.
 */
void qio_net_listener_disconnect(QIONetListener *listener);




#endif /* QIO_NET_LISTENER_H */

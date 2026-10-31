/*
 * QEMU I/O channels sockets driver
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#ifndef QIO_CHANNEL_SOCKET_H
#define QIO_CHANNEL_SOCKET_H

#include "io/channel.h"
#include "io/task.h"
#include "qemu/sockets.h"
#include "qom/object.h"

#define TYPE_QIO_CHANNEL_SOCKET "qio-channel-socket"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelSocket, QIO_CHANNEL_SOCKET)


/**
 * QIOChannelSocket:
 *
 * The QIOChannelSocket class provides a channel implementation
 * that can transport data over a UNIX socket or TCP socket.
 * Beyond the core channel API, it also provides functionality
 * for accepting client connections, tuning some socket
 * parameters and getting socket address strings.
 */

struct QIOChannelSocket {
    QIOChannel parent;
    int fd;
    struct sockaddr_storage localAddr;
    socklen_t localAddrLen;
    struct sockaddr_storage remoteAddr;
    socklen_t remoteAddrLen;
    ssize_t zero_copy_queued;
    ssize_t zero_copy_sent;
    bool blocking;
    /**
     * This flag indicates whether any new data was successfully sent with
     * zerocopy since the last qio_channel_socket_flush() call.
     */
    bool new_zero_copy_sent_success;
};


/**
 * qio_channel_socket_new:
 *
 * Create a channel for performing I/O on a socket
 * connection, that is initially closed. After
 * creating the socket, it must be setup as a client
 * connection or server.
 *
 * Returns: the socket channel object
 */
QIOChannelSocket *
qio_channel_socket_new(void);

/**
 * qio_channel_socket_new_fd:
 * @fd: the socket file descriptor
 * @errp: pointer to a NULL-initialized error object
 *
 * Create a channel for performing I/O on the socket
 * connection represented by the file descriptor @fd.
 *
 * Returns: the socket channel object, or NULL on error
 */
QIOChannelSocket *
qio_channel_socket_new_fd(int fd,
                          Error **errp);






/**
 * qio_channel_socket_accept:
 * @ioc: the socket channel object
 * @errp: pointer to a NULL-initialized error object
 *
 * If the socket represents a server, then this accepts
 * a new client connection. The returned channel will
 * represent the connected client socket.
 *
 * Returns: the new client channel, or NULL on error
 */
QIOChannelSocket *
qio_channel_socket_accept(QIOChannelSocket *ioc,
                          Error **errp);

#endif /* QIO_CHANNEL_SOCKET_H */

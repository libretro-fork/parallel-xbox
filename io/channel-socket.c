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
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"
#include "qemu/module.h"
#include "io/channel-socket.h"
#include "io/channel-util.h"
#include "io/channel-watch.h"
#include "trace.h"
#include "qapi/clone-visitor.h"
#ifdef CONFIG_LINUX
#include <linux/errqueue.h>
#include <sys/socket.h>

#if (defined(MSG_ZEROCOPY) && defined(SO_ZEROCOPY))
#define QEMU_MSG_ZEROCOPY
#endif
#endif

#define SOCKET_MAX_FDS 16

#ifdef QEMU_MSG_ZEROCOPY
static int qio_channel_socket_flush_internal(QIOChannel *ioc,
                                             bool block,
                                             Error **errp);
#endif






struct QIOChannelListenWorkerData {
    SocketAddress *addr;
    int num; /* amount of expected connections */
};




struct QIOChannelSocketDGramWorkerData {
    SocketAddress *localAddr;
    SocketAddress *remoteAddr;
};




static void qio_channel_socket_init(Object *obj)
{
    QIOChannelSocket *ioc = QIO_CHANNEL_SOCKET(obj);
    ioc->fd = -1;
}

static void qio_channel_socket_finalize(Object *obj)
{
    QIOChannelSocket *ioc = QIO_CHANNEL_SOCKET(obj);

    if (ioc->fd != -1) {
        QIOChannel *ioc_local = QIO_CHANNEL(ioc);
        if (qio_channel_has_feature(ioc_local, QIO_CHANNEL_FEATURE_LISTEN)) {
            Error *err = NULL;

            socket_listen_cleanup(ioc->fd, &err);
            if (err) {
                error_report_err(err);
                err = NULL;
            }
        }
#ifdef WIN32
        qemu_socket_unselect_nofail(ioc->fd);
#endif
        close(ioc->fd);
        ioc->fd = -1;
    }
}


#ifndef WIN32
static void qio_channel_socket_copy_fds(struct msghdr *msg,
                                        int **fds, size_t *nfds)
{
    struct cmsghdr *cmsg;

    *nfds = 0;
    *fds = NULL;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        int fd_size;
        int gotfds;

        if (cmsg->cmsg_len < CMSG_LEN(sizeof(int)) ||
            cmsg->cmsg_level != SOL_SOCKET ||
            cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }

        fd_size = cmsg->cmsg_len - CMSG_LEN(0);

        if (!fd_size) {
            continue;
        }

        gotfds = fd_size / sizeof(int);
        *fds = g_renew(int, *fds, *nfds + gotfds);
        memcpy(*fds + *nfds, CMSG_DATA(cmsg), fd_size);
        *nfds += gotfds;
    }
}

static bool qio_channel_handle_fds(int *fds, size_t nfds,
                                   bool preserve_blocking, Error **errp)
{
    int *end = fds + nfds, *fd;

#ifdef MSG_CMSG_CLOEXEC
    if (preserve_blocking) {
        /* Nothing to do */
        return true;
    }
#endif

    for (fd = fds; fd != end; fd++) {
        if (*fd < 0) {
            continue;
        }

        if (!preserve_blocking) {
            /* O_NONBLOCK is preserved across SCM_RIGHTS so reset it */
            if (!qemu_set_blocking(*fd, true, errp)) {
                return false;
            }
        }

#ifndef MSG_CMSG_CLOEXEC
        qemu_set_cloexec(*fd);
#endif
    }

    return true;
}

static void qio_channel_cleanup_fds(int **fds, size_t *nfds)
{
    for (size_t i = 0; i < *nfds; i++) {
        if ((*fds)[i] < 0) {
            continue;
        }
        close((*fds)[i]);
    }

    g_clear_pointer(fds, g_free);
    *nfds = 0;
}


static ssize_t qio_channel_socket_readv(QIOChannel *ioc,
                                        const struct iovec *iov,
                                        size_t niov,
                                        int **fds,
                                        size_t *nfds,
                                        int flags,
                                        Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    ssize_t ret;
    struct msghdr msg = { NULL, };
    char control[CMSG_SPACE(sizeof(int) * SOCKET_MAX_FDS)];
    int sflags = 0;

    memset(control, 0, CMSG_SPACE(sizeof(int) * SOCKET_MAX_FDS));

    msg.msg_iov = (struct iovec *)iov;
    msg.msg_iovlen = niov;
    if (fds && nfds) {
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
#ifdef MSG_CMSG_CLOEXEC
        sflags |= MSG_CMSG_CLOEXEC;
#endif

    }

    if (flags & QIO_CHANNEL_READ_FLAG_MSG_PEEK) {
        sflags |= MSG_PEEK;
    }

 retry:
    ret = recvmsg(sioc->fd, &msg, sflags);
    if (ret < 0) {
        if (errno == EAGAIN) {
            return QIO_CHANNEL_ERR_BLOCK;
        }
        if (errno == EINTR) {
            goto retry;
        }

        error_setg_errno(errp, errno,
                         "Unable to read from socket");
        return -1;
    }

    if (fds && nfds) {
        bool preserve_blocking =
            flags & QIO_CHANNEL_READ_FLAG_FD_PRESERVE_BLOCKING;

        qio_channel_socket_copy_fds(&msg, fds, nfds);

        if (!qio_channel_handle_fds(*fds, *nfds,
                                    preserve_blocking, errp)) {
            qio_channel_cleanup_fds(fds, nfds);
            return -1;
        }
    }

    return ret;
}

static ssize_t qio_channel_socket_writev(QIOChannel *ioc,
                                         const struct iovec *iov,
                                         size_t niov,
                                         int *fds,
                                         size_t nfds,
                                         int flags,
                                         Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    ssize_t ret;
    struct msghdr msg = { NULL, };
    char control[CMSG_SPACE(sizeof(int) * SOCKET_MAX_FDS)];
    size_t fdsize = sizeof(int) * nfds;
    struct cmsghdr *cmsg;
    int sflags = 0;
#ifdef QEMU_MSG_ZEROCOPY
    bool blocking = sioc->blocking;
    bool zerocopy_flushed_once = false;
#endif

    memset(control, 0, CMSG_SPACE(sizeof(int) * SOCKET_MAX_FDS));

    msg.msg_iov = (struct iovec *)iov;
    msg.msg_iovlen = niov;

    if (nfds) {
        if (nfds > SOCKET_MAX_FDS) {
            error_setg_errno(errp, EINVAL,
                             "Only %d FDs can be sent, got %zu",
                             SOCKET_MAX_FDS, nfds);
            return -1;
        }

        msg.msg_control = control;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * nfds);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(fdsize);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cmsg), fds, fdsize);
    }

    if (flags & QIO_CHANNEL_WRITE_FLAG_ZERO_COPY) {
#ifdef QEMU_MSG_ZEROCOPY
        sflags = MSG_ZEROCOPY;
#else
        /*
         * We expect QIOChannel class entry point to have
         * blocked this code path already
         */
        g_assert_not_reached();
#endif
    }

 retry:
    ret = sendmsg(sioc->fd, &msg, sflags);
    if (ret <= 0) {
        switch (errno) {
        case EAGAIN:
            return QIO_CHANNEL_ERR_BLOCK;
        case EINTR:
            goto retry;
#ifdef QEMU_MSG_ZEROCOPY
        case ENOBUFS:
            if (flags & QIO_CHANNEL_WRITE_FLAG_ZERO_COPY) {
                /**
                 * Socket error queueing may exhaust the OPTMEM limit. Try
                 * flushing the error queue once.
                 */
                if (!zerocopy_flushed_once) {
                    ret = qio_channel_socket_flush_internal(ioc, blocking,
                                                            errp);
                    if (ret < 0) {
                        return -1;
                    }
                    zerocopy_flushed_once = true;
                    goto retry;
                } else {
                    error_setg_errno(errp, errno,
                                     "Process can't lock enough memory for "
                                     "using MSG_ZEROCOPY");
                    return -1;
                }
            }
            break;
#endif
        }

        error_setg_errno(errp, errno,
                         "Unable to write to socket");
        return -1;
    }

    if (flags & QIO_CHANNEL_WRITE_FLAG_ZERO_COPY) {
        sioc->zero_copy_queued++;
    }

    return ret;
}
#else /* WIN32 */
static ssize_t qio_channel_socket_readv(QIOChannel *ioc,
                                        const struct iovec *iov,
                                        size_t niov,
                                        int **fds,
                                        size_t *nfds,
                                        int flags,
                                        Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    ssize_t done = 0;
    ssize_t i;
    int sflags = 0;

    if (flags & QIO_CHANNEL_READ_FLAG_MSG_PEEK) {
        sflags |= MSG_PEEK;
    }

    for (i = 0; i < niov; i++) {
        ssize_t ret;
    retry:
        ret = recv(sioc->fd,
                   iov[i].iov_base,
                   iov[i].iov_len,
                   sflags);
        if (ret < 0) {
            if (errno == EAGAIN) {
                if (done) {
                    return done;
                } else {
                    return QIO_CHANNEL_ERR_BLOCK;
                }
            } else if (errno == EINTR) {
                goto retry;
            } else {
                error_setg_errno(errp, errno,
                                 "Unable to read from socket");
                return -1;
            }
        }
        done += ret;
        if (ret < iov[i].iov_len) {
            return done;
        }
    }

    return done;
}

static ssize_t qio_channel_socket_writev(QIOChannel *ioc,
                                         const struct iovec *iov,
                                         size_t niov,
                                         int *fds,
                                         size_t nfds,
                                         int flags,
                                         Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    ssize_t done = 0;
    ssize_t i;

    for (i = 0; i < niov; i++) {
        ssize_t ret;
    retry:
        ret = send(sioc->fd,
                   iov[i].iov_base,
                   iov[i].iov_len,
                   0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                if (done) {
                    return done;
                } else {
                    return QIO_CHANNEL_ERR_BLOCK;
                }
            } else if (errno == EINTR) {
                goto retry;
            } else {
                error_setg_errno(errp, errno,
                                 "Unable to write to socket");
                return -1;
            }
        }
        done += ret;
        if (ret < iov[i].iov_len) {
            return done;
        }
    }

    return done;
}
#endif /* WIN32 */


#ifdef QEMU_MSG_ZEROCOPY
static int qio_channel_socket_flush_internal(QIOChannel *ioc,
                                             bool block,
                                             Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    struct msghdr msg = {};
    struct sock_extended_err *serr;
    struct cmsghdr *cm;
    char control[CMSG_SPACE(sizeof(*serr))];
    int received;

    if (sioc->zero_copy_queued == sioc->zero_copy_sent) {
        return 0;
    }

    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    memset(control, 0, sizeof(control));

    while (sioc->zero_copy_sent < sioc->zero_copy_queued) {
        received = recvmsg(sioc->fd, &msg, MSG_ERRQUEUE);
        if (received < 0) {
            switch (errno) {
            case EAGAIN:
                if (block) {
                    /*
                     * Nothing on errqueue, wait until something is
                     * available.
                     *
                     * Use G_IO_ERR instead of G_IO_IN since MSG_ERRQUEUE reads
                     * are signaled via POLLERR, not POLLIN, as the kernel
                     * sets POLLERR when zero-copy notificatons appear on the
                     * socket error queue.
                     */
                    qio_channel_wait(ioc, G_IO_ERR);
                    continue;
                }
                return 0;
            case EINTR:
                continue;
            default:
                error_setg_errno(errp, errno,
                                 "Unable to read errqueue");
                return -1;
            }
        }

        cm = CMSG_FIRSTHDR(&msg);
        if (cm->cmsg_level != SOL_IP   && cm->cmsg_type != IP_RECVERR &&
            cm->cmsg_level != SOL_IPV6 && cm->cmsg_type != IPV6_RECVERR) {
            error_setg_errno(errp, EPROTOTYPE,
                             "Wrong cmsg in errqueue");
            return -1;
        }

        serr = (void *) CMSG_DATA(cm);
        if (serr->ee_errno != SO_EE_ORIGIN_NONE) {
            error_setg_errno(errp, serr->ee_errno,
                             "Error on socket");
            return -1;
        }
        if (serr->ee_origin != SO_EE_ORIGIN_ZEROCOPY) {
            error_setg_errno(errp, serr->ee_origin,
                             "Error not from zero copy");
            return -1;
        }
        if (serr->ee_data < serr->ee_info) {
            error_setg_errno(errp, serr->ee_origin,
                             "Wrong notification bounds");
            return -1;
        }

        /* No errors, count successfully finished sendmsg()*/
        sioc->zero_copy_sent += serr->ee_data - serr->ee_info + 1;

        /* If any sendmsg() succeeded using zero copy, mark zerocopy success */
        if (serr->ee_code != SO_EE_CODE_ZEROCOPY_COPIED) {
            sioc->new_zero_copy_sent_success = true;
        }
    }

    return 0;
}

static int qio_channel_socket_flush(QIOChannel *ioc,
                                    Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    int ret;

    ret = qio_channel_socket_flush_internal(ioc, true, errp);
    if (ret < 0) {
        return ret;
    }

    if (sioc->new_zero_copy_sent_success) {
        sioc->new_zero_copy_sent_success = false;
        return 0;
    }

    return 1;
}

#endif /* QEMU_MSG_ZEROCOPY */

static int
qio_channel_socket_set_blocking(QIOChannel *ioc,
                                bool enabled,
                                Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    sioc->blocking = enabled;

    if (!qemu_set_blocking(sioc->fd, enabled, errp)) {
        return -1;
    }

    return 0;
}


static void
qio_channel_socket_set_delay(QIOChannel *ioc,
                             bool enabled)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    int v = enabled ? 0 : 1;

    setsockopt(sioc->fd,
               IPPROTO_TCP, TCP_NODELAY,
               &v, sizeof(v));
}


static void
qio_channel_socket_set_cork(QIOChannel *ioc,
                            bool enabled)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    int v = enabled ? 1 : 0;

    socket_set_cork(sioc->fd, v);
}

static int
qio_channel_socket_get_peerpid(QIOChannel *ioc,
                               unsigned int *pid,
                               Error **errp)
{
#ifdef CONFIG_LINUX
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    Error *err = NULL;
    socklen_t len = sizeof(struct ucred);

    struct ucred cred;
    if (getsockopt(sioc->fd,
               SOL_SOCKET, SO_PEERCRED,
               &cred, &len) == -1) {
        error_setg_errno(&err, errno, "Unable to get peer credentials");
        error_propagate(errp, err);
        *pid = -1;
        return -1;
    }
    *pid = (unsigned int)cred.pid;
    return 0;
#else
    error_setg(errp, "Unsupported feature");
    *pid = -1;
    return -1;
#endif
}

static int
qio_channel_socket_close(QIOChannel *ioc,
                         Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    int rc = 0;
    Error *err = NULL;

    if (sioc->fd != -1) {
#ifdef WIN32
        qemu_socket_unselect_nofail(sioc->fd);
#endif
        if (qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_LISTEN)) {
            socket_listen_cleanup(sioc->fd, errp);
        }

        if (close(sioc->fd) < 0) {
            sioc->fd = -1;
            error_setg_errno(&err, errno, "Unable to close socket");
            error_propagate(errp, err);
            return -1;
        }
        sioc->fd = -1;
    }
    return rc;
}

static int
qio_channel_socket_shutdown(QIOChannel *ioc,
                            QIOChannelShutdown how,
                            Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    int sockhow;

    switch (how) {
    case QIO_CHANNEL_SHUTDOWN_READ:
        sockhow = SHUT_RD;
        break;
    case QIO_CHANNEL_SHUTDOWN_WRITE:
        sockhow = SHUT_WR;
        break;
    case QIO_CHANNEL_SHUTDOWN_BOTH:
    default:
        sockhow = SHUT_RDWR;
        break;
    }

    if (shutdown(sioc->fd, sockhow) < 0) {
        error_setg_errno(errp, errno,
                         "Unable to shutdown socket");
        return -1;
    }
    return 0;
}

static void qio_channel_socket_set_aio_fd_handler(QIOChannel *ioc,
                                                  AioContext *read_ctx,
                                                  IOHandler *io_read,
                                                  AioContext *write_ctx,
                                                  IOHandler *io_write,
                                                  void *opaque)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);

    qio_channel_util_set_aio_fd_handler(sioc->fd, read_ctx, io_read,
                                        sioc->fd, write_ctx, io_write,
                                        opaque);
}

static GSource *qio_channel_socket_create_watch(QIOChannel *ioc,
                                                GIOCondition condition)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    return qio_channel_create_socket_watch(ioc,
                                           sioc->fd,
                                           condition);
}

static void qio_channel_socket_class_init(ObjectClass *klass,
                                          const void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_socket_writev;
    ioc_klass->io_readv = qio_channel_socket_readv;
    ioc_klass->io_set_blocking = qio_channel_socket_set_blocking;
    ioc_klass->io_close = qio_channel_socket_close;
    ioc_klass->io_shutdown = qio_channel_socket_shutdown;
    ioc_klass->io_set_cork = qio_channel_socket_set_cork;
    ioc_klass->io_set_delay = qio_channel_socket_set_delay;
    ioc_klass->io_create_watch = qio_channel_socket_create_watch;
    ioc_klass->io_set_aio_fd_handler = qio_channel_socket_set_aio_fd_handler;
#ifdef QEMU_MSG_ZEROCOPY
    ioc_klass->io_flush = qio_channel_socket_flush;
#endif
    ioc_klass->io_peerpid = qio_channel_socket_get_peerpid;
}

static const TypeInfo qio_channel_socket_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_SOCKET,
    .instance_size = sizeof(QIOChannelSocket),
    .instance_init = qio_channel_socket_init,
    .instance_finalize = qio_channel_socket_finalize,
    .class_init = qio_channel_socket_class_init,
};

static void qio_channel_socket_register_types(void)
{
    type_register_static(&qio_channel_socket_info);
}

type_init(qio_channel_socket_register_types);

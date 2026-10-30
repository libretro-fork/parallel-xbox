/*
 * QEMU I/O channels
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

#include "qemu/osdep.h"
#include "block/aio-wait.h"
#include "io/channel.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/iov.h"

bool qio_channel_has_feature(QIOChannel *ioc,
                             QIOChannelFeature feature)
{
    return ioc->features & (1 << feature);
}




ssize_t qio_channel_readv_full(QIOChannel *ioc,
                               const struct iovec *iov,
                               size_t niov,
                               int **fds,
                               size_t *nfds,
                               int flags,
                               Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if ((fds || nfds) &&
        !qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_FD_PASS)) {
        error_setg_errno(errp, EINVAL,
                         "Channel does not support file descriptor passing");
        return -1;
    }

    if ((flags & QIO_CHANNEL_READ_FLAG_MSG_PEEK) &&
        !qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_READ_MSG_PEEK)) {
        error_setg_errno(errp, EINVAL,
                         "Channel does not support peek read");
        return -1;
    }

    return klass->io_readv(ioc, iov, niov, fds, nfds, flags, errp);
}


ssize_t qio_channel_writev_full(QIOChannel *ioc,
                                const struct iovec *iov,
                                size_t niov,
                                int *fds,
                                size_t nfds,
                                int flags,
                                Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (fds || nfds) {
        if (!qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_FD_PASS)) {
            error_setg_errno(errp, EINVAL,
                             "Channel does not support file descriptor passing");
            return -1;
        }
        if (flags & QIO_CHANNEL_WRITE_FLAG_ZERO_COPY) {
            error_setg_errno(errp, EINVAL,
                             "Zero Copy does not support file descriptor passing");
            return -1;
        }
    }

    if ((flags & QIO_CHANNEL_WRITE_FLAG_ZERO_COPY) &&
        !qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_WRITE_ZERO_COPY)) {
        error_setg_errno(errp, EINVAL,
                         "Requested Zero Copy feature is not available");
        return -1;
    }

    return klass->io_writev(ioc, iov, niov, fds, nfds, flags, errp);
}


int coroutine_mixed_fn qio_channel_writev_all(QIOChannel *ioc,
                                              const struct iovec *iov,
                                              size_t niov,
                                              Error **errp)
{
    return qio_channel_writev_full_all(ioc, iov, niov, NULL, 0, 0, errp);
}

int coroutine_mixed_fn qio_channel_writev_full_all(QIOChannel *ioc,
                                                   const struct iovec *iov,
                                                   size_t niov,
                                                   int *fds, size_t nfds,
                                                   int flags, Error **errp)
{
    int ret = -1;
    struct iovec *local_iov = g_new(struct iovec, niov);
    struct iovec *local_iov_head = local_iov;
    unsigned int nlocal_iov = niov;

    nlocal_iov = iov_copy(local_iov, nlocal_iov,
                          iov, niov,
                          0, iov_size(iov, niov));

    while (nlocal_iov > 0) {
        ssize_t len;

        len = qio_channel_writev_full(ioc, local_iov, nlocal_iov, fds,
                                            nfds, flags, errp);

        if (len == QIO_CHANNEL_ERR_BLOCK) {
            qio_channel_wait_cond(ioc, G_IO_OUT);
            continue;
        }
        if (len < 0) {
            goto cleanup;
        }

        iov_discard_front(&local_iov, &nlocal_iov, len);

        fds = NULL;
        nfds = 0;
    }

    ret = 0;
 cleanup:
    g_free(local_iov_head);
    return ret;
}










int qio_channel_close(QIOChannel *ioc,
                      Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);
    return klass->io_close(ioc, errp);
}


GSource *qio_channel_create_watch(QIOChannel *ioc,
                                  GIOCondition condition)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);
    GSource *ret = klass->io_create_watch(ioc, condition);

    if (ioc->name) {
        g_source_set_name(ret, ioc->name);
    }

    return ret;
}



int qio_channel_shutdown(QIOChannel *ioc,
                         QIOChannelShutdown how,
                         Error **errp)
{
    QIOChannelClass *klass = QIO_CHANNEL_GET_CLASS(ioc);

    if (!klass->io_shutdown) {
        error_setg(errp, "Data path shutdown not supported");
        return -1;
    }

    return klass->io_shutdown(ioc, how, errp);
}




static gboolean qio_channel_wait_complete(QIOChannel *ioc,
                                          GIOCondition condition,
                                          gpointer opaque)
{
    GMainLoop *loop = opaque;

    g_main_loop_quit(loop);
    return FALSE;
}


void qio_channel_wait(QIOChannel *ioc,
                      GIOCondition condition)
{
    GMainContext *ctxt = g_main_context_new();
    GMainLoop *loop = g_main_loop_new(ctxt, TRUE);
    GSource *source;

    source = qio_channel_create_watch(ioc, condition);

    g_source_set_callback(source,
                          (GSourceFunc)qio_channel_wait_complete,
                          loop,
                          NULL);

    g_source_attach(source, ctxt);

    g_main_loop_run(loop);

    g_source_unref(source);
    g_main_loop_unref(loop);
    g_main_context_unref(ctxt);
}

void qio_channel_wait_cond(QIOChannel *ioc,
                           GIOCondition condition)
{
    qio_channel_wait(ioc, condition);
}

static void qio_channel_finalize(Object *obj)
{
    QIOChannel *ioc = QIO_CHANNEL(obj);


    g_free(ioc->name);

#ifdef _WIN32
    if (ioc->event) {
        CloseHandle(ioc->event);
    }
#endif
}

static const TypeInfo qio_channel_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QIO_CHANNEL,
    .instance_size = sizeof(QIOChannel),
    .instance_finalize = qio_channel_finalize,
    .abstract = true,
    .class_size = sizeof(QIOChannelClass),
};


static void qio_channel_register_types(void)
{
    type_register_static(&qio_channel_info);
}


type_init(qio_channel_register_types);

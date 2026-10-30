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

#include "qemu/osdep.h"
#include "io/net-listener.h"
#include "io/dns-resolver.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "trace.h"

struct QIONetListenerSource {
    QIOChannelSocket *sioc;
    GSource *io_source;
    QIONetListener *listener;
};




/* listener->lock must be held by caller. */
static void
qio_net_listener_unwatch(QIONetListener *listener, const char *caller)
{
    size_t i;

    if (!listener->io_func) {
        return;
    }

    trace_qio_net_listener_unwatch(listener, listener->io_func,
                                   listener->context, listener->aio_context,
                                   caller);
    for (i = 0; i < listener->nsioc; i++) {
        if (!listener->aio_context) {
            if (listener->source[i]->io_source) {
                g_source_destroy(listener->source[i]->io_source);
                g_source_unref(listener->source[i]->io_source);
                listener->source[i]->io_source = NULL;
            }
        } else {
            assert(listener->context == NULL);
            qio_channel_set_aio_fd_handler(
                QIO_CHANNEL(listener->source[i]->sioc),
                listener->aio_context, NULL, NULL, NULL, NULL);
            if (i == listener->nsioc - 1) {
                object_unref(OBJECT(listener));
            }
        }
    }
}


struct QIONetListenerClientWaitData {
    QIOChannelSocket *sioc;
    GMainLoop *loop;
};


void qio_net_listener_disconnect(QIONetListener *listener)
{
    size_t i;

    if (!listener->connected) {
        return;
    }

    QEMU_LOCK_GUARD(&listener->lock);
    qio_net_listener_unwatch(listener, "disconnect");
    for (i = 0; i < listener->nsioc; i++) {
        qio_channel_close(QIO_CHANNEL(listener->source[i]->sioc), NULL);
    }
    listener->connected = false;
}


static void qio_net_listener_finalize(Object *obj)
{
    QIONetListener *listener = QIO_NET_LISTENER(obj);
    size_t i;

    qio_net_listener_disconnect(listener);
    if (listener->io_notify) {
        listener->io_notify(listener->io_data);
    }

    for (i = 0; i < listener->nsioc; i++) {
        object_unref(OBJECT(listener->source[i]->sioc));
        g_free(listener->source[i]);
    }
    g_free(listener->source);
    g_free(listener->name);
    qemu_mutex_destroy(&listener->lock);
}

static const TypeInfo qio_net_listener_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_QIO_NET_LISTENER,
    .instance_size = sizeof(QIONetListener),
    .instance_finalize = qio_net_listener_finalize,
};


static void qio_net_listener_register_types(void)
{
    type_register_static(&qio_net_listener_info);
}


type_init(qio_net_listener_register_types);

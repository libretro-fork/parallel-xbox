/*
 * QEMU aio implementation
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "block/block.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "qemu/rcu_queue.h"
#include "qemu/sockets.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "aio-posix.h"

/* The frontend uses GSource descriptor readiness directly. There is no
 * synchronous POSIX AIO poller or adaptive monitor backend in this core. */

static void aio_add_ready_handler(AioHandlerList *ready_list,
                           AioHandler *node,
                           int revents)
{
    QLIST_SAFE_REMOVE_RCU(node, node_ready); /* remove from nested parent's list */
    node->pfd.revents = revents;
    QLIST_INSERT_HEAD_RCU(ready_list, node, node_ready);
}

static void aio_update_source_poll(AioContext *ctx,
                              AioHandler *old_node,
                              AioHandler *new_node)
{
    if (old_node) {
        /*
         * If the GSource is in the process of being destroyed then
         * g_source_remove_poll() causes an assertion failure.  Skip removal in
         * that case, because glib cleans up its state during destruction
         * anyway.
         */
        if (!g_source_is_destroyed(&ctx->source)) {
            g_source_remove_poll(&ctx->source, &old_node->pfd);
        }
    }

    if (new_node) {
        g_source_add_poll(&ctx->source, &new_node->pfd);
    }
}

static AioHandler *find_aio_handler(AioContext *ctx, int fd)
{
    AioHandler *node;

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (node->pfd.fd == fd) {
            if (!QLIST_IS_INSERTED_RCU(node, node_deleted)) {
                return node;
            }
        }
    }

    return NULL;
}

static bool aio_remove_fd_handler(AioContext *ctx, AioHandler *node)
{
    node->pfd.revents = 0;

    /* If the fd monitor has already marked it deleted, leave it alone */
    if (QLIST_IS_INSERTED_RCU(node, node_deleted)) {
        return false;
    }

    /* If a read is in progress, just mark the node as deleted */
    if (ctx->handler_walk_depth) {
        QLIST_INSERT_HEAD_RCU(&ctx->deleted_aio_handlers, node, node_deleted);
        return false;
    }
    /* Otherwise, delete it for real.  We can't just mark it as
     * deleted because deleted nodes are only cleaned up while
     * no one is walking the handlers list.
     */
    QLIST_SAFE_REMOVE_RCU(node, node);
    return true;
}

void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        AioPollFn *io_poll,
                        IOHandler *io_poll_ready,
                        void *opaque)
{
    AioHandler *node;
    AioHandler *new_node = NULL;
    bool is_new = false;
    bool deleted = false;

    if (io_poll && !io_poll_ready) {
        io_poll = NULL; /* polling only makes sense if there is a handler */
    }

    aio_assert_handler_owner(ctx);

    node = find_aio_handler(ctx, fd);

    /* Are we deleting the fd handler? */
    if (!io_read && !io_write && !io_poll) {
        if (node == NULL) {
            return;
        }
        /* Clear readiness before unregistering the source poll descriptor. */
        node->pfd.events = 0;
    } else {
        if (node == NULL) {
            is_new = true;
        }
        /* Alloc and insert if it's not already there */
        new_node = g_new0(AioHandler, 1);

        /* Update handler with latest information */
        new_node->io_read = io_read;
        new_node->io_write = io_write;
        new_node->opaque = opaque;

        if (is_new) {
            new_node->pfd.fd = fd;
        } else {
            new_node->pfd = node->pfd;
        }

        new_node->pfd.events = (io_read ? G_IO_IN | G_IO_HUP | G_IO_ERR : 0);
        new_node->pfd.events |= (io_write ? G_IO_OUT | G_IO_ERR : 0);

        QLIST_INSERT_HEAD_RCU(&ctx->aio_handlers, new_node, node);
    }

    aio_update_source_poll(ctx, node, new_node);
    if (node) {
        deleted = aio_remove_fd_handler(ctx, node);
    }
    aio_notify(ctx);

    if (deleted) {
        g_free(node);
    }
}

void aio_set_event_notifier(AioContext *ctx,
                            EventNotifier *notifier,
                            EventNotifierHandler *io_read,
                            AioPollFn *io_poll,
                            EventNotifierHandler *io_poll_ready)
{
    aio_set_fd_handler(ctx, event_notifier_get_fd(notifier),
                       (IOHandler *)io_read, NULL, io_poll,
                       (IOHandler *)io_poll_ready, notifier);
}

bool aio_prepare(AioContext *ctx)
{
    aio_assert_handler_owner(ctx);
    return false;
}

bool aio_pending(AioContext *ctx)
{
    AioHandler *node;
    bool result = false;

    /*
     * We have to walk very carefully in case aio_set_fd_handler is
     * called while we're walking.
     */
    aio_handler_walk_begin(ctx);

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        int revents = node->pfd.revents & node->pfd.events;

        if (revents & (G_IO_IN | G_IO_HUP | G_IO_ERR) && node->io_read) {
            result = true;
            break;
        }
        if (revents & (G_IO_OUT | G_IO_ERR) && node->io_write) {
            result = true;
            break;
        }
    }

    aio_handler_walk_end(ctx);

    return result;
}

static void aio_free_deleted_handlers(AioContext *ctx)
{
    AioHandler *node;

    if (QLIST_EMPTY_RCU(&ctx->deleted_aio_handlers)) {
        return;
    }
    if (!aio_handlers_can_reclaim(ctx)) {
        return; /* we are nested, let the parent do the freeing */
    }

    while ((node = QLIST_FIRST_RCU(&ctx->deleted_aio_handlers))) {
        QLIST_SAFE_REMOVE_RCU(node, node);
        QLIST_SAFE_REMOVE_RCU(node, node_deleted);
        g_free(node);
    }
}

static bool aio_dispatch_handler(AioContext *ctx, AioHandler *node)
{
    bool progress = false;
    int revents;

    revents = node->pfd.revents & node->pfd.events;
    node->pfd.revents = 0;

    if (!QLIST_IS_INSERTED_RCU(node, node_deleted) &&
        (revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) &&
        node->io_read) {
        node->io_read(node->opaque);

        /* aio_notify() does not count as progress */
        if (node->opaque != &ctx->notifier) {
            progress = true;
        }
    }
    if (!QLIST_IS_INSERTED_RCU(node, node_deleted) &&
        (revents & (G_IO_OUT | G_IO_ERR)) &&
        node->io_write) {
        node->io_write(node->opaque);
        progress = true;
    }

    return progress;
}

static void aio_collect_ready_handlers(AioContext *ctx,
                                        AioHandlerList *ready_list)
{
    AioHandler *node;

    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        int revents = node->pfd.revents;

        if (revents) {
            aio_add_ready_handler(ready_list, node, revents);
        }
    }
}

static bool aio_dispatch_ready_handlers(AioContext *ctx,
                                        AioHandlerList *ready_list)
{
    bool progress = false;
    AioHandler *node;

    while ((node = QLIST_FIRST_RCU(ready_list))) {
        QLIST_SAFE_REMOVE_RCU(node, node_ready);
        progress = aio_dispatch_handler(ctx, node) || progress;

    }

    return progress;
}

void aio_dispatch(AioContext *ctx)
{
    AioHandlerList ready_list = QLIST_RCU_HEAD_INITIALIZER(ready_list);

    aio_handler_walk_begin(ctx);

    aio_bh_poll(ctx);

    aio_collect_ready_handlers(ctx, &ready_list);
    aio_dispatch_ready_handlers(ctx, &ready_list);

    aio_free_deleted_handlers(ctx);
    aio_handler_walk_end(ctx);

}

bool aio_context_setup(AioContext *ctx, Error **errp)
{
    aio_assert_handler_owner(ctx);
    return true;
}

void aio_context_destroy(AioContext *ctx)
{
    aio_assert_handler_owner(ctx);
    aio_free_deleted_handlers(ctx);
}

void aio_context_set_aio_params(AioContext *ctx, int64_t max_batch)
{
    /*
     * No thread synchronization here, it doesn't matter if an incorrect value
     * is used once.
     */
    ctx->aio_max_batch = max_batch;

    aio_notify(ctx);
}

bool aio_has_fd_handlers(AioContext *ctx)
{
    AioHandler *node;

    aio_assert_handler_owner(ctx);
    QLIST_FOREACH_RCU(node, &ctx->aio_handlers, node) {
        if (!QLIST_IS_INSERTED_RCU(node, node_deleted) &&
            node->opaque != &ctx->notifier) {
            return true;
        }
    }
    return false;
}

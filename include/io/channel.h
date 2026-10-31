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

#ifndef QIO_CHANNEL_H
#define QIO_CHANNEL_H

#include "qom/object.h"
#include "qemu/coroutine-core.h"
#include "block/aio.h"

#define TYPE_QIO_CHANNEL "qio-channel"
OBJECT_DECLARE_TYPE(QIOChannel, QIOChannelClass,
                    QIO_CHANNEL)


#define QIO_CHANNEL_ERR_BLOCK -2

#define QIO_CHANNEL_WRITE_FLAG_ZERO_COPY 0x1

#define QIO_CHANNEL_READ_FLAG_MSG_PEEK 0x1
#define QIO_CHANNEL_READ_FLAG_RELAXED_EOF 0x2
#define QIO_CHANNEL_READ_FLAG_FD_PRESERVE_BLOCKING 0x4

typedef enum QIOChannelFeature QIOChannelFeature;

enum QIOChannelFeature {
    QIO_CHANNEL_FEATURE_FD_PASS,
    QIO_CHANNEL_FEATURE_SHUTDOWN,
    QIO_CHANNEL_FEATURE_LISTEN,
    QIO_CHANNEL_FEATURE_WRITE_ZERO_COPY,
    QIO_CHANNEL_FEATURE_READ_MSG_PEEK,
    QIO_CHANNEL_FEATURE_SEEKABLE,
    QIO_CHANNEL_FEATURE_CONCURRENT_IO,
};


typedef enum QIOChannelShutdown QIOChannelShutdown;

enum QIOChannelShutdown {
    QIO_CHANNEL_SHUTDOWN_READ = 1,
    QIO_CHANNEL_SHUTDOWN_WRITE = 2,
    QIO_CHANNEL_SHUTDOWN_BOTH = 3,
};

typedef gboolean (*QIOChannelFunc)(QIOChannel *ioc,
                                   GIOCondition condition,
                                   gpointer data);

/**
 * QIOChannel:
 *
 * The QIOChannel defines the core API for a generic I/O channel
 * class hierarchy. It is inspired by GIOChannel, but has the
 * following differences
 *
 *  - Use QOM to properly support arbitrary subclassing
 *  - Support use of iovecs for efficient I/O with multiple blocks
 *  - None of the character set translation, binary data exclusively
 *  - Direct support for QEMU Error object reporting
 *  - File descriptor passing
 *
 * This base class is abstract so cannot be instantiated. There
 * will be subclasses for dealing with sockets, files, and higher
 * level protocols such as TLS, WebSocket, etc.
 */

struct QIOChannel {
    Object parent;
    unsigned int features; /* bitmask of QIOChannelFeatures */
    char *name;
#ifdef _WIN32
    HANDLE event; /* For use with GSource on Win32 */
#endif
};

/**
 * QIOChannelClass:
 *
 * This class defines the contract that all subclasses
 * must follow to provide specific channel implementations.
 * The first five callbacks are mandatory to support, others
 * provide additional optional features.
 *
 * Consult the corresponding public API docs for a description
 * of the semantics of each callback. io_shutdown in particular
 * must be thread-safe, terminate quickly and must not block.
 */
struct QIOChannelClass {
    ObjectClass parent;

    /* Mandatory callbacks */
    ssize_t (*io_writev)(QIOChannel *ioc,
                         const struct iovec *iov,
                         size_t niov,
                         int *fds,
                         size_t nfds,
                         int flags,
                         Error **errp);

    /*
     * The io_readv handler must guarantee that all
     * incoming fds are set BLOCKING (unless
     * QIO_CHANNEL_READ_FLAG_FD_PRESERVE_BLOCKING flag is set) and
     * CLOEXEC (if available).
     * @fds and @nfds are set only on success path. Still, setting
     * @fds and @nfds to zero is acceptable on failure path.
     */
    ssize_t (*io_readv)(QIOChannel *ioc,
                        const struct iovec *iov,
                        size_t niov,
                        int **fds,
                        size_t *nfds,
                        int flags,
                        Error **errp);

    int (*io_close)(QIOChannel *ioc,
                    Error **errp);
    GSource * (*io_create_watch)(QIOChannel *ioc,
                                 GIOCondition condition);
    int (*io_set_blocking)(QIOChannel *ioc,
                           bool enabled,
                           Error **errp);

    /* Optional callbacks */
    ssize_t (*io_pwritev)(QIOChannel *ioc,
                          const struct iovec *iov,
                          size_t niov,
                          off_t offset,
                          Error **errp);
    ssize_t (*io_preadv)(QIOChannel *ioc,
                         const struct iovec *iov,
                         size_t niov,
                         off_t offset,
                         Error **errp);
    int (*io_shutdown)(QIOChannel *ioc,
                       QIOChannelShutdown how,
                       Error **errp);
    void (*io_set_cork)(QIOChannel *ioc,
                        bool enabled);
    void (*io_set_delay)(QIOChannel *ioc,
                         bool enabled);
    off_t (*io_seek)(QIOChannel *ioc,
                     off_t offset,
                     int whence,
                     Error **errp);
    void (*io_set_aio_fd_handler)(QIOChannel *ioc,
                                  AioContext *read_ctx,
                                  IOHandler *io_read,
                                  AioContext *write_ctx,
                                  IOHandler *io_write,
                                  void *opaque);
    int (*io_flush)(QIOChannel *ioc,
                    Error **errp);
    int (*io_peerpid)(QIOChannel *ioc,
                       unsigned int *pid,
                       Error **errp);
};

/* General I/O handling functions */

/**
 * qio_channel_has_feature:
 * @ioc: the channel object
 * @feature: the feature to check support of
 *
 * Determine whether the channel implementation supports
 * the optional feature named in @feature.
 *
 * Returns: true if supported, false otherwise.
 */
bool qio_channel_has_feature(QIOChannel *ioc,
                             QIOChannelFeature feature);

/**
 * qio_channel_readv_full:
 * @ioc: the channel object
 * @iov: the array of memory regions to read data into
 * @niov: the length of the @iov array
 * @fds: pointer to an array that will received file handles
 * @nfds: pointer filled with number of elements in @fds on return
 * @flags: read flags (QIO_CHANNEL_READ_FLAG_*)
 * @errp: pointer to a NULL-initialized error object
 *
 * Read data from the IO channel, storing it in the
 * memory regions referenced by @iov. Each element
 * in the @iov will be fully populated with data
 * before the next one is used. The @niov parameter
 * specifies the total number of elements in @iov.
 *
 * It is not required for all @iov to be filled with
 * data. If the channel is in blocking mode, at least
 * one byte of data will be read, but no more is
 * guaranteed. If the channel is non-blocking and no
 * data is available, it will return QIO_CHANNEL_ERR_BLOCK
 *
 * If the channel has passed any file descriptors,
 * the @fds array pointer will be allocated and
 * the elements filled with the received file
 * descriptors. The @nfds pointer will be updated
 * to indicate the size of the @fds array that
 * was allocated. It is the callers responsibility
 * to call close() on each file descriptor and to
 * call g_free() on the array pointer in @fds.
 * @fds allocated and set (and @nfds is set too)
 * _only_ on success path. Still, @fds and @nfds
 * may be set to zero on failure path.
 * qio_channel_readv_full() guarantees that all
 * incoming fds are set BLOCKING (unless
 * QIO_CHANNEL_READ_FLAG_FD_PRESERVE_BLOCKING flag
 * is set) and CLOEXEC (if available).
 *
 * It is an error to pass a non-NULL @fds parameter
 * unless qio_channel_has_feature() returns a true
 * value for the QIO_CHANNEL_FEATURE_FD_PASS constant.
 *
 * Returns: the number of bytes read, or -1 on error,
 * or QIO_CHANNEL_ERR_BLOCK if no data is available
 * and the channel is non-blocking
 */
ssize_t qio_channel_readv_full(QIOChannel *ioc,
                               const struct iovec *iov,
                               size_t niov,
                               int **fds,
                               size_t *nfds,
                               int flags,
                               Error **errp);


/**
 * qio_channel_writev_full:
 * @ioc: the channel object
 * @iov: the array of memory regions to write data from
 * @niov: the length of the @iov array
 * @fds: an array of file handles to send
 * @nfds: number of file handles in @fds
 * @flags: write flags (QIO_CHANNEL_WRITE_FLAG_*)
 * @errp: pointer to a NULL-initialized error object
 *
 * Write data to the IO channel, reading it from the
 * memory regions referenced by @iov. Each element
 * in the @iov will be fully sent, before the next
 * one is used. The @niov parameter specifies the
 * total number of elements in @iov.
 *
 * It is not required for all @iov data to be fully
 * sent. If the channel is in blocking mode, at least
 * one byte of data will be sent, but no more is
 * guaranteed. If the channel is non-blocking and no
 * data can be sent, it will return QIO_CHANNEL_ERR_BLOCK
 *
 * If there are file descriptors to send, the @fds
 * array should be non-NULL and provide the handles.
 * All file descriptors will be sent if at least one
 * byte of data was sent.
 *
 * It is an error to pass a non-NULL @fds parameter
 * unless qio_channel_has_feature() returns a true
 * value for the QIO_CHANNEL_FEATURE_FD_PASS constant.
 *
 * Returns: the number of bytes sent, or -1 on error,
 * or QIO_CHANNEL_ERR_BLOCK if no data is can be sent
 * and the channel is non-blocking
 */
ssize_t qio_channel_writev_full(QIOChannel *ioc,
                                const struct iovec *iov,
                                size_t niov,
                                int *fds,
                                size_t nfds,
                                int flags,
                                Error **errp);


/**
 * qio_channel_writev_all:
 * @ioc: the channel object
 * @iov: the array of memory regions to write data from
 * @niov: the length of the @iov array
 * @errp: pointer to a NULL-initialized error object
 *
 * Write data to the IO channel, reading it from the
 * memory regions referenced by @iov. Each element
 * in the @iov will be fully sent, before the next
 * one is used. The @niov parameter specifies the
 * total number of elements in @iov.
 *
 * The function will wait for all requested data
 * to be written, yielding from the current coroutine
 * if required.
 *
 * Returns: 0 if all bytes were written, or -1 on error
 */
int coroutine_mixed_fn qio_channel_writev_all(QIOChannel *ioc,
                                              const struct iovec *iov,
                                              size_t niov,
                                              Error **errp);


/**
 * qio_channel_close:
 * @ioc: the channel object
 * @errp: pointer to a NULL-initialized error object
 *
 * Close the channel, flushing any pending I/O
 *
 * Returns: 0 on success, -1 on error
 */
int qio_channel_close(QIOChannel *ioc,
                      Error **errp);

/**
 * qio_channel_shutdown:
 * @ioc: the channel object
 * @how: the direction to shutdown
 * @errp: pointer to a NULL-initialized error object
 *
 * Shutdowns transmission and/or receiving of data
 * without closing the underlying transport.
 *
 * Not all implementations will support this facility,
 * so may report an error. To avoid errors, the
 * caller may check for the feature flag
 * QIO_CHANNEL_FEATURE_SHUTDOWN prior to calling
 * this method.
 *
 * This function is thread-safe, terminates quickly and does not block.
 *
 * Returns: 0 on success, -1 on error
 */
int qio_channel_shutdown(QIOChannel *ioc,
                         QIOChannelShutdown how,
                         Error **errp);



/**
 * qio_channel_create_watch:
 * @ioc: the channel object
 * @condition: the I/O condition to monitor
 *
 * Create a new main loop source that is used to watch
 * for the I/O condition @condition. Typically the
 * qio_channel_add_watch() method would be used instead
 * of this, since it directly attaches a callback to
 * the source
 *
 * Returns: the new main loop source.
 */
GSource *qio_channel_create_watch(QIOChannel *ioc,
                                  GIOCondition condition);


/**
 * qio_channel_wait:
 * @ioc: the channel object
 * @condition: the I/O condition to wait for
 *
 * Block execution from the current thread until
 * the condition indicated by @condition becomes
 * available.
 *
 * This will enter a nested event loop to perform
 * the wait.
 */
void qio_channel_wait(QIOChannel *ioc,
                      GIOCondition condition);

/**
 * qio_channel_wait_cond:
 * @ioc: the channel object
 * @condition: the I/O condition to wait for
 *
 * Block execution from the current thread until
 * the condition indicated by @condition becomes
 * available.
 *
 * The core runs no coroutines, so this is qio_channel_wait().
 */
void qio_channel_wait_cond(QIOChannel *ioc,
                           GIOCondition condition);

/**
 * qio_channel_readv_full_all_eof:
 * @ioc: the channel object
 * @iov: the array of memory regions to read data to
 * @niov: the length of the @iov array
 * @fds: an array of file handles to read
 * @nfds: number of file handles in @fds
 * @flags: read flags (QIO_CHANNEL_READ_FLAG_*)
 * @errp: pointer to a NULL-initialized error object
 *
 *
 * Performs same function as qio_channel_readv_all_eof.
 * Additionally, attempts to read file descriptors shared
 * over the channel. The function will wait for all
 * requested data to be read, yielding from the current
 * coroutine if required. data refers to both file
 * descriptors and the iovs.
 *
 * Returns: 1 if all bytes were read, 0 if end-of-file
 *          occurs without data, or -1 on error
 */

/**
 * qio_channel_readv_full_all:
 * @ioc: the channel object
 * @iov: the array of memory regions to read data to
 * @niov: the length of the @iov array
 * @fds: an array of file handles to read
 * @nfds: number of file handles in @fds
 * @errp: pointer to a NULL-initialized error object
 *
 *
 * Performs same function as qio_channel_readv_all_eof.
 * Additionally, attempts to read file descriptors shared
 * over the channel. The function will wait for all
 * requested data to be read, yielding from the current
 * coroutine if required. data refers to both file
 * descriptors and the iovs.
 *
 * Returns: 0 if all bytes were read, or -1 on error
 */

/**
 * qio_channel_writev_full_all:
 * @ioc: the channel object
 * @iov: the array of memory regions to write data from
 * @niov: the length of the @iov array
 * @fds: an array of file handles to send
 * @nfds: number of file handles in @fds
 * @flags: write flags (QIO_CHANNEL_WRITE_FLAG_*)
 * @errp: pointer to a NULL-initialized error object
 *
 *
 * Behaves like qio_channel_writev_full but will attempt
 * to send all data passed (file handles and memory regions).
 * The function will wait for all requested data
 * to be written, yielding from the current coroutine
 * if required.
 *
 * If QIO_CHANNEL_WRITE_FLAG_ZERO_COPY is passed in flags,
 * instead of waiting for all requested data to be written,
 * this function will wait until it's all queued for writing.
 * In this case, if the buffer gets changed between queueing and
 * sending, the updated buffer will be sent. If this is not a
 * desired behavior, it's suggested to call qio_channel_flush()
 * before reusing the buffer.
 *
 * Returns: 0 if all bytes were written, or -1 on error
 */

int coroutine_mixed_fn qio_channel_writev_full_all(QIOChannel *ioc,
                                                   const struct iovec *iov,
                                                   size_t niov,
                                                   int *fds, size_t nfds,
                                                   int flags, Error **errp);

#endif /* QIO_CHANNEL_H */

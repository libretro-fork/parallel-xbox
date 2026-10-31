/*
 * AioContext POSIX event loop implementation internal APIs
 *
 * Copyright IBM, Corp. 2008
 * Copyright Red Hat, Inc. 2020
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

#ifndef AIO_POSIX_H
#define AIO_POSIX_H

#include "block/aio.h"
#include "qapi/error.h"

struct AioHandler {
    GPollFD pfd;
    IOHandler *io_read;
    IOHandler *io_write;
    void *opaque;
    QLIST_RCU_ENTRY(AioHandler) node;
    QLIST_RCU_ENTRY(AioHandler) node_ready; /* nested dispatch readiness */
    QLIST_RCU_ENTRY(AioHandler) node_deleted;
};

#endif /* AIO_POSIX_H */

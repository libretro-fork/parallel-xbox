/*
 * QEMU Block backends
 *
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_BACKEND_IO_H
#define BLOCK_BACKEND_IO_H

#include "block-backend-common.h"
#include "block/accounting.h"

/*
 * I/O API functions. These functions are thread-safe.
 *
 * See include/block/block-io.h for more information about
 * the I/O API.
 */

const char *blk_name(const BlockBackend *blk);

BlockDriverState *blk_bs(BlockBackend *blk);


/*
 * Return the qdev ID, or if no ID is assigned the QOM path,
 * of the block device attached to the BlockBackend.
 *
 * The caller is responsible for releasing the value returned
 * with g_free() after use.
 */

BlockAIOCB *blk_aio_preadv(BlockBackend *blk, int64_t offset,
                           QEMUIOVector *qiov, BdrvRequestFlags flags,
                           BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *blk_aio_pwritev(BlockBackend *blk, int64_t offset,
                            QEMUIOVector *qiov, BdrvRequestFlags flags,
                            BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *blk_aio_flush(BlockBackend *blk,
                          BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *blk_aio_pdiscard(BlockBackend *blk, int64_t offset, int64_t bytes,
                             BlockCompletionFunc *cb, void *opaque);
void blk_aio_cancel_async(BlockAIOCB *acb);

void blk_inc_in_flight(BlockBackend *blk);
void blk_dec_in_flight(BlockBackend *blk);

bool blk_is_inserted(BlockBackend *blk);


void blk_lock_medium(BlockBackend *blk, bool locked);

void blk_eject(BlockBackend *blk, bool eject_flag);

int64_t blk_getlength(BlockBackend *blk);

void blk_get_geometry(BlockBackend *blk, uint64_t *nb_sectors_ptr);


void *blk_blockalign(BlockBackend *blk, size_t size);
bool blk_is_writable(BlockBackend *blk);
bool blk_enable_write_cache(BlockBackend *blk);
BlockdevOnError blk_get_on_error(BlockBackend *blk, bool is_read);
BlockErrorAction blk_get_error_action(BlockBackend *blk, bool is_read,
                                      int error);
void blk_error_action(BlockBackend *blk, BlockErrorAction action,
                      bool is_read, int error);
int blk_get_max_hw_iov(BlockBackend *blk);

AioContext *blk_get_aio_context(BlockBackend *blk);
BlockAcctStats *blk_get_stats(BlockBackend *blk);
void *blk_aio_get(const AIOCBInfo *aiocb_info, BlockBackend *blk,
                  BlockCompletionFunc *cb, void *opaque);
BlockAIOCB *blk_abort_aio_request(BlockBackend *blk,
                                  BlockCompletionFunc *cb,
                                  void *opaque, int ret);

uint64_t blk_get_max_hw_transfer(BlockBackend *blk);


/*
 * "I/O or GS" API functions. These functions can run without
 * the BQL, but only in one specific iothread/main loop.
 *
 * See include/block/block-io.h for more information about
 * the "I/O or GS" API.
 */

int blk_pread(BlockBackend *blk, int64_t offset,
                               int64_t bytes, void *buf,
                               BdrvRequestFlags flags);



int blk_pwrite(BlockBackend *blk, int64_t offset,
                                int64_t bytes, const void *buf,
                                BdrvRequestFlags flags);




int blk_pwrite_zeroes(BlockBackend *blk, int64_t offset,
                                       int64_t bytes,
                                       BdrvRequestFlags flags);



int blk_flush(BlockBackend *blk);



#endif /* BLOCK_BACKEND_IO_H */

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

#ifndef BLOCK_BACKEND_GLOBAL_STATE_H
#define BLOCK_BACKEND_GLOBAL_STATE_H

#include "block-backend-common.h"

/*
 * Global state (GS) API. These functions run under the BQL.
 *
 * See include/block/block-global-state.h for more information about
 * the GS API.
 */

BlockBackend *blk_new(AioContext *ctx, uint64_t perm, uint64_t shared_perm);

BlockBackend * blk_new_with_bs(BlockDriverState *bs, uint64_t perm, uint64_t shared_perm,
                Error **errp);

BlockBackend *
blk_co_new_with_bs(BlockDriverState *bs, uint64_t perm, uint64_t shared_perm,
                   Error **errp);

BlockBackend * blk_new_open(const char *filename, const char *reference, QDict *options,
             int flags, Error **errp);

BlockBackend *
blk_co_new_open(const char *filename, const char *reference, QDict *options,
                int flags, Error **errp);


void blk_unref(BlockBackend *blk);

BlockBackend *blk_by_name(const char *name);

int blk_insert_bs(BlockBackend *blk, BlockDriverState *bs, Error **errp);
int blk_replace_bs(BlockBackend *blk, BlockDriverState *new_bs, Error **errp);
int GRAPH_UNLOCKED blk_set_perm(BlockBackend *blk, uint64_t perm,
                                uint64_t shared_perm, Error **errp);

void blk_iostatus_enable(BlockBackend *blk);
int blk_attach_dev(BlockBackend *blk, DeviceState *dev);
void blk_detach_dev(BlockBackend *blk, DeviceState *dev);
BlockBackend *blk_by_dev(void *dev);
void blk_set_dev_ops(BlockBackend *blk, const BlockDevOps *ops, void *opaque);

void blk_aio_cancel(BlockAIOCB *acb);
int blk_commit_all(void);
void GRAPH_UNLOCKED blk_drain(BlockBackend *blk);
void blk_set_on_error(BlockBackend *blk, BlockdevOnError on_read_error,
                      BlockdevOnError on_write_error);
bool blk_supports_write_perm(BlockBackend *blk);
void blk_set_enable_write_cache(BlockBackend *blk, bool wce);

int blk_probe_blocksizes(BlockBackend *blk, BlockSizes *bsz);
int blk_probe_geometry(BlockBackend *blk, HDGeometry *geo);



#endif /* BLOCK_BACKEND_GLOBAL_STATE_H */

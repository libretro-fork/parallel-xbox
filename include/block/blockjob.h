/*
 * Declarations for long-running block device operations
 *
 * Copyright (c) 2011 IBM Corp.
 * Copyright (c) 2012 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef BLOCKJOB_H
#define BLOCKJOB_H

#include "qapi/qapi-types-block-core.h"
#include "qemu/job.h"
#include "qemu/ratelimit.h"

#define BLOCK_JOB_SLICE_TIME 100000000ULL /* ns */

typedef struct BlockJobDriver BlockJobDriver;

/**
 * BlockJob:
 *
 * Long-running operation on a BlockDriverState.
 */
typedef struct BlockJob {
    /**
     * Data belonging to the generic Job infrastructure.
     * Protected by job mutex.
     */
    Job job;

    /**
     * Status that is published by the query-block-jobs QMP API.
     * Protected by job mutex.
     */
    BlockDeviceIoStatus iostatus;

    /**
     * Speed that was set with @block_job_set_speed.
     * Always modified and read under the BQL (GLOBAL_STATE_CODE).
     */
    int64_t speed;

    /**
     * Rate limiting data structure for implementing @speed.
     * RateLimit API is thread-safe.
     */
    RateLimit limit;

    /**
     * Block other operations when block job is running.
     * Always modified and read under the BQL (GLOBAL_STATE_CODE).
     */
    Error *blocker;

    /** All notifiers are set once in block_job_create() and never modified. */

    /** Called when a cancelled job is finalised. */
    Notifier finalize_cancelled_notifier;

    /** Called when a successfully completed job is finalised. */
    Notifier finalize_completed_notifier;

    /** Called when the job transitions to PENDING */
    Notifier pending_notifier;

    /** Called when the job transitions to READY */
    Notifier ready_notifier;

    /** Called when the job coroutine yields or terminates */
    Notifier idle_notifier;

    /**
     * BlockDriverStates that are involved in this block job.
     * Always modified and read under the BQL (GLOBAL_STATE_CODE).
     */
    GSList *nodes;
} BlockJob;

/*
 * Global state (GS) API. These functions run under the BQL.
 *
 * See include/block/block-global-state.h for more information about
 * the GS API.
 */

/* Same as block_job_get(), but called with job lock held. */

/**
 * block_job_add_bdrv:
 * @job: A block job
 * @name: The name to assign to the new BdrvChild
 * @bs: A BlockDriverState that is involved in @job
 * @perm, @shared_perm: Permissions to request on the node
 *
 * Add @bs to the list of BlockDriverState that are involved in
 * @job. This means that all operations will be blocked on @bs while
 * @job exists.
 *
 * All block nodes must be drained.
 */
int GRAPH_WRLOCK
block_job_add_bdrv(BlockJob *job, const char *name, BlockDriverState *bs,
                   uint64_t perm, uint64_t shared_perm, Error **errp);




/*
 * Common functions that are neither I/O nor Global State.
 *
 * See include/block/block-common.h for more information about
 * the Common API.
 */

#endif

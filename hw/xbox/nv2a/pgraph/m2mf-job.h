/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_M2MF_JOB_H
#define HW_XBOX_NV2A_PGRAPH_M2MF_JOB_H

#include "m2mf-transfer.h"

/* All state belongs to the FIFO owner. Backend completion must be polled or
 * delivered to that owner; this is not a cross-thread publication mechanism.
 * No GPU handle, pointer, allocation or wait is retained in this state. */
typedef enum PGRAPHM2MFPhase {
    PGRAPH_M2MF_IDLE,
    PGRAPH_M2MF_PREPARE,
    PGRAPH_M2MF_CPU_COPY,
    PGRAPH_M2MF_GPU_WAIT,
    PGRAPH_M2MF_PUBLISH,
    PGRAPH_M2MF_COMPLETE,
    PGRAPH_M2MF_FAILED,
} PGRAPHM2MFPhase;

typedef struct PGRAPHM2MFJob {
    PGRAPHM2MFTransfer transfer;
    /* Runtime token: do not restore it from a guest save-state. Loading must
     * revoke backend work before establishing a fresh token. */
    uint64_t ticket;
    PGRAPHM2MFPhase phase;
} PGRAPHM2MFJob;

static inline bool pgraph_m2mf_job_active(const PGRAPHM2MFJob *job)
{
    return job->phase == PGRAPH_M2MF_PREPARE ||
           job->phase == PGRAPH_M2MF_CPU_COPY ||
           job->phase == PGRAPH_M2MF_GPU_WAIT ||
           job->phase == PGRAPH_M2MF_PUBLISH;
}

static inline uint64_t pgraph_m2mf_job_next_ticket(PGRAPHM2MFJob *job)
{
    if (++job->ticket == 0) {
        ++job->ticket;
    }
    return job->ticket;
}

/* Reset revokes old completion tokens. The backend must also cancel or retire
 * its resource ownership before reusing GPU storage; this helper cannot undo
 * submitted GPU writes or RAM bytes already copied. */
static inline void pgraph_m2mf_job_reset(PGRAPHM2MFJob *job)
{
    pgraph_m2mf_job_next_ticket(job);
    job->phase = PGRAPH_M2MF_IDLE;
    job->transfer = (PGRAPHM2MFTransfer){0};
}

/* Return a nonzero token for a new attempt, zero if an earlier command still
 * owns the FIFO slot. Invalid ranges become FAILED without touching buffers.
 * The caller translates FAILED into the appropriate guest error policy. */
static inline uint64_t pgraph_m2mf_job_start(PGRAPHM2MFJob *job,
                                             const PGRAPHM2MFTransfer *layout,
                                             size_t in_bytes, size_t out_bytes)
{
    if (pgraph_m2mf_job_active(job)) {
        return 0;
    }
    job->transfer = *layout;
    pgraph_m2mf_job_next_ticket(job);
    PGRAPHM2MFResult result = pgraph_m2mf_begin(&job->transfer,
                                               in_bytes, out_bytes);
    job->phase = result == PGRAPH_M2MF_INVALID ? PGRAPH_M2MF_FAILED :
                 result == PGRAPH_M2MF_DONE ? PGRAPH_M2MF_PUBLISH :
                                            PGRAPH_M2MF_PREPARE;
    return job->ticket;
}

/* For CPU execution the backend must first establish source/destination RAM
 * authority, preserve untouched destination bytes and arrange dirty tracking
 * before any payload writes. For GPU execution it must establish equivalent
 * visibility/ownership and submit the copy before acknowledging preparation.
 * A not-ready backend simply makes no transition and returns to its owner. */
static inline bool pgraph_m2mf_job_prepared(PGRAPHM2MFJob *job,
                                            uint64_t ticket, bool gpu)
{
    if (ticket != job->ticket || job->phase != PGRAPH_M2MF_PREPARE) {
        return false;
    }
    job->phase = gpu ? PGRAPH_M2MF_GPU_WAIT : PGRAPH_M2MF_CPU_COPY;
    return true;
}

static inline PGRAPHM2MFResult pgraph_m2mf_job_cpu_step(PGRAPHM2MFJob *job,
                                                       uint64_t ticket,
                                                       const uint8_t *in,
                                                       uint8_t *out,
                                                       size_t budget)
{
    if (ticket != job->ticket || job->phase != PGRAPH_M2MF_CPU_COPY) {
        return PGRAPH_M2MF_INVALID;
    }
    PGRAPHM2MFResult result = pgraph_m2mf_step(&job->transfer, in, out, budget);
    if (result == PGRAPH_M2MF_DONE) {
        job->phase = PGRAPH_M2MF_PUBLISH;
    }
    return result;
}

/* Call only after a nonblocking backend retirement check establishes that
 * this copy is complete and visible. A stale or duplicate event is ignored. */
static inline bool pgraph_m2mf_job_gpu_retired(PGRAPHM2MFJob *job,
                                               uint64_t ticket)
{
    if (ticket != job->ticket || job->phase != PGRAPH_M2MF_GPU_WAIT) {
        return false;
    }
    job->phase = PGRAPH_M2MF_PUBLISH;
    return true;
}

/* Acknowledge after the owner writes the notifier with required dirty tracking
 * and ordering. Until then the command remains active and cannot be replaced.
 * This does not itself write a notifier or release the FIFO command. */
static inline bool pgraph_m2mf_job_published(PGRAPHM2MFJob *job,
                                             uint64_t ticket)
{
    if (ticket != job->ticket || job->phase != PGRAPH_M2MF_PUBLISH) {
        return false;
    }
    job->phase = PGRAPH_M2MF_COMPLETE;
    return true;
}
#endif

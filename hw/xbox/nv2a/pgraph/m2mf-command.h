/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_M2MF_COMMAND_H
#define HW_XBOX_NV2A_PGRAPH_M2MF_COMMAND_H

#include "m2mf-job.h"
#include "hw/xbox/nv2a/nv2a_regs.h"

/* Object/DMA references are guest RAMIN instances, already resolved by PFIFO,
 * not host pointers. Layout lives in the job; setup is frozen while pending.
 * Device/subchannel context switching must preserve this object's ownership. */
typedef struct PGRAPHM2MFCommand {
    PGRAPHM2MFJob job;
    uint32_t object_instance, dma_notify, dma_in, dma_out;
    uint32_t format, notify_argument;
    uint8_t owner_subchannel;
    bool pending;
} PGRAPHM2MFCommand;

typedef enum PGRAPHM2MFDispatch {
    PGRAPH_M2MF_UNHANDLED,
    PGRAPH_M2MF_CONSUMED,
    PGRAPH_M2MF_START,
    PGRAPH_M2MF_RETRY,
    PGRAPH_M2MF_FAULT,
} PGRAPHM2MFDispatch;

/* Backend resource ownership must be revoked separately before reset. */
static inline void pgraph_m2mf_command_reset(PGRAPHM2MFCommand *command)
{
    pgraph_m2mf_job_reset(&command->job);
    uint64_t ticket = command->job.ticket;
    *command = (PGRAPHM2MFCommand){0};
    command->job.ticket = ticket;
}

/* For an error, call only after the owner has applied its guest fault policy.
 * Success is acknowledged by the matching BUFFER_NOTIFY retry below. Neither
 * case writes a notifier, advances DMA GET or cancels host resources here. */
static inline bool pgraph_m2mf_command_ack(PGRAPHM2MFCommand *command)
{
    if (!command->pending ||
        (command->job.phase != PGRAPH_M2MF_COMPLETE &&
         command->job.phase != PGRAPH_M2MF_FAILED)) {
        return false;
    }
    command->pending = false;
    command->job.phase = PGRAPH_M2MF_IDLE;
    return true;
}

/* START is returned exactly once per BUFFER_NOTIFY word. The owner must map
 * and validate the DMA objects, start the job, and return zero consumption to
 * PFIFO while pending. RETRY never resets progress or mutates setup. A later
 * matching retry consumes the command only after notifier publication. */
static inline PGRAPHM2MFDispatch pgraph_m2mf_decode(PGRAPHM2MFCommand *command,
                                                   unsigned subchannel,
                                                   uint32_t method,
                                                   uint32_t argument)
{
    if (subchannel >= 8) {
        return PGRAPH_M2MF_UNHANDLED;
    }
    if (command->pending) {
        if (method != NV039_BUFFER_NOTIFY ||
            subchannel != command->owner_subchannel ||
            argument != command->notify_argument) {
            return PGRAPH_M2MF_RETRY;
        }
        if (command->job.phase == PGRAPH_M2MF_FAILED) {
            return PGRAPH_M2MF_FAULT;
        }
        if (command->job.phase != PGRAPH_M2MF_COMPLETE) {
            return PGRAPH_M2MF_RETRY;
        }
        pgraph_m2mf_command_ack(command);
        return PGRAPH_M2MF_CONSUMED;
    }
    PGRAPHM2MFTransfer *t = &command->job.transfer;
    switch (method) {
    case NV039_SET_OBJECT: command->object_instance = argument; break;
    case NV039_NO_OPERATION: break;
    case NV039_SET_CONTEXT_DMA_NOTIFIES: command->dma_notify = argument; break;
    case NV039_SET_CONTEXT_DMA_BUFFER_IN: command->dma_in = argument; break;
    case NV039_SET_CONTEXT_DMA_BUFFER_OUT: command->dma_out = argument; break;
    case NV039_OFFSET_IN: t->offset_in = argument; break;
    case NV039_OFFSET_OUT: t->offset_out = argument; break;
    case NV039_PITCH_IN: t->pitch_in = argument; break;
    case NV039_PITCH_OUT: t->pitch_out = argument; break;
    case NV039_LINE_LENGTH_IN: t->length = argument; break;
    case NV039_LINE_COUNT: t->rows = argument; break;
    case NV039_FORMAT:
        command->format = argument;
        t->stride_in = argument & 0xff;
        t->stride_out = (argument >> 8) & 0xff;
        break;
    case NV039_BUFFER_NOTIFY:
        command->notify_argument = argument;
        command->owner_subchannel = subchannel;
        command->pending = true;
        return PGRAPH_M2MF_START;
    default:
        return PGRAPH_M2MF_UNHANDLED;
    }
    return PGRAPH_M2MF_CONSUMED;
}
#endif

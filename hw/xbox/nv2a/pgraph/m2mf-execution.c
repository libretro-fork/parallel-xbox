/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "hw/xbox/nv2a/nv2a_int.h"
#include "m2mf-execution.h"
#include <retro_atomic.h>

static bool latch_binding(NV2AState *d, uint32_t instance,
                           PGRAPHM2MFBinding *binding)
{
    NVDMAView view;
    if (!nv_dma_map_checked(d, instance, &view) ||
        view.address > UINT32_MAX || view.bytes > UINT32_MAX) {
        return false;
    }
    *binding = (PGRAPHM2MFBinding){view.address, view.bytes,
                                  view.dma_class, view.dma_target};
    return true;
}

static uint8_t *binding_data(NV2AState *d, const PGRAPHM2MFBinding *binding)
{
    hwaddr bytes = memory_region_size(d->vram);
    if (binding->address > bytes || binding->bytes > bytes - binding->address) {
        return NULL;
    }
    return d->vram_ptr + binding->address;
}

PGRAPHM2MFResult pgraph_m2mf_execution_start(NV2AState *d,
    PGRAPHM2MFCommand *command, PGRAPHM2MFExecution *execution,
    bool notify_enabled)
{
    if (!command->pending || command->job.phase != PGRAPH_M2MF_IDLE) {
        return PGRAPH_M2MF_INVALID;
    }
    *execution = (PGRAPHM2MFExecution){0};
    bool payload = command->job.transfer.length && command->job.transfer.rows;
    if ((payload &&
         (!latch_binding(d, command->dma_in, &execution->in) ||
          !latch_binding(d, command->dma_out, &execution->out))) ||
        (notify_enabled &&
         (!latch_binding(d, command->dma_notify, &execution->notify) ||
          execution->notify.bytes < 0x20 ||
          (execution->notify.address & 3)))) {
        pgraph_m2mf_job_next_ticket(&command->job);
        command->job.phase = PGRAPH_M2MF_FAILED;
        return PGRAPH_M2MF_INVALID;
    }
    execution->has_notify = notify_enabled;
    execution->ticket = pgraph_m2mf_job_start(&command->job,
        &command->job.transfer, execution->in.bytes, execution->out.bytes);
    return command->job.phase == PGRAPH_M2MF_FAILED ? PGRAPH_M2MF_INVALID :
           command->job.phase == PGRAPH_M2MF_PUBLISH ? PGRAPH_M2MF_DONE :
                                                      PGRAPH_M2MF_YIELD;
}

static bool cpu_binding(const PGRAPHM2MFBinding *binding, bool write)
{
    return binding->dma_target == NV_DMA_TARGET_NVM &&
           (binding->dma_class == NV_DMA_IN_MEMORY_CLASS ||
            binding->dma_class == (write ? NV_DMA_TO_MEMORY_CLASS :
                                           NV_DMA_FROM_MEMORY_CLASS));
}

PGRAPHM2MFResult pgraph_m2mf_execution_cpu_step(NV2AState *d,
    PGRAPHM2MFCommand *command, const PGRAPHM2MFExecution *execution,
    size_t budget)
{
    PGRAPHM2MFJob *job = &command->job;
    if (!command->pending || job->ticket != execution->ticket ||
        job->phase != PGRAPH_M2MF_CPU_COPY ||
        !cpu_binding(&execution->in, false) || !cpu_binding(&execution->out, true)) {
        return PGRAPH_M2MF_INVALID;
    }
    uint8_t *in = binding_data(d, &execution->in);
    uint8_t *out = binding_data(d, &execution->out);
    if (!in || !out) {
        return PGRAPH_M2MF_INVALID;
    }
    budget = MIN(budget, (size_t)PGRAPH_M2MF_CPU_MAX_BYTES);
    /* Bound per-call bookkeeping as well as bytes. Do not scan/dirty the whole
     * transfer footprint on each dispatch, especially with sparse pitches. */
    for (unsigned rows = 0; budget && rows < 256; rows++) {
        PGRAPHM2MFTransfer *t = &job->transfer;
        size_t n = MIN(budget, (size_t)t->length - t->column);
        size_t src = t->offset_in + (size_t)t->row * t->pitch_in;
        size_t dst = t->offset_out + (size_t)t->row * t->pitch_out;
        size_t x = t->column;
        if (t->stride_in == 1 && t->stride_out == 1) {
            uintptr_t s = (uintptr_t)(in + src), q = (uintptr_t)(out + dst);
            if (q > s && q - s < t->length) {
                x = t->length - t->column - n;
            }
        }
        size_t src_span = (n - 1) * t->stride_in + 1;
        size_t dst_span = (n - 1) * t->stride_out + 1;
        hwaddr src_addr = execution->in.address + src + x * t->stride_in;
        hwaddr dst_addr = execution->out.address + dst + x * t->stride_out;
        /* These callbacks never wait. Ahead pages require renderer preparation;
         * do not consume stale bytes or overwrite an ahead destination. */
        if (pgraph_cpu_transfer_access(&d->pgraph, src_addr, src_span, false) ||
            pgraph_cpu_transfer_access(&d->pgraph, dst_addr, dst_span, true)) {
            return PGRAPH_M2MF_YIELD;
        }
        memory_region_set_dirty(d->vram, dst_addr, dst_span);
        PGRAPHM2MFResult result = pgraph_m2mf_job_cpu_step(job,
            execution->ticket, in, out, n);
        if (result != PGRAPH_M2MF_YIELD) {
            return result;
        }
        budget -= n;
    }
    return PGRAPH_M2MF_YIELD;
}

PGRAPHM2MFResult pgraph_m2mf_execution_publish(NV2AState *d,
    PGRAPHM2MFCommand *command, const PGRAPHM2MFExecution *execution)
{
    if (!command->pending || command->job.ticket != execution->ticket ||
        command->job.phase != PGRAPH_M2MF_PUBLISH) {
        return PGRAPH_M2MF_INVALID;
    }
    if (execution->has_notify) {
        if (!cpu_binding(&execution->notify, true)) {
            return PGRAPH_M2MF_INVALID;
        }
        uint8_t *base = binding_data(d, &execution->notify);
        if (!base) {
            return PGRAPH_M2MF_INVALID;
        }
        hwaddr address = execution->notify.address + 0x10;
        if (pgraph_cpu_transfer_access(&d->pgraph, address, 16, true)) {
            return PGRAPH_M2MF_YIELD;
        }
        uint64_t timestamp = ptimer_snapshot(d);
        memory_region_set_dirty(d->vram, address, 16);
        stl_le_p(base + 0x10, timestamp);
        stl_le_p(base + 0x14, timestamp >> 32);
        stl_le_p(base + 0x18, 0);
        /* Aligned status zero is endian-independent. Order all prior data and
         * timestamp writes before the guest can observe completion. */
        retro_atomic_store_release_int((retro_atomic_int_t *)(base + 0x1c), 0);
    }
    pgraph_m2mf_job_published(&command->job, execution->ticket);
    return PGRAPH_M2MF_DONE;
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_M2MF_EXECUTION_H
#define HW_XBOX_NV2A_PGRAPH_M2MF_EXECUTION_H
#include "m2mf-command.h"

/* Physical bindings latched once per command. Hardware RAM is at most 128 MiB;
 * checked conversion keeps this owner state compact without host pointers. */
typedef struct PGRAPHM2MFBinding {
    uint32_t address, bytes, dma_class, dma_target;
} PGRAPHM2MFBinding;
typedef struct PGRAPHM2MFExecution {
    PGRAPHM2MFBinding in, out, notify;
    uint64_t ticket;
    bool has_notify;
} PGRAPHM2MFExecution;

#define PGRAPH_M2MF_CPU_MAX_BYTES (64 * 1024)

struct NV2AState;
/* notify_enabled is explicit: instance zero must not be guessed to mean an
 * unbound notifier. DMA bindings and command layout are immutable afterwards.
 * The owner still selects/prepares the renderer path before CPU execution. */
PGRAPHM2MFResult pgraph_m2mf_execution_start(struct NV2AState *d,
    PGRAPHM2MFCommand *command, PGRAPHM2MFExecution *execution,
    bool notify_enabled);
PGRAPHM2MFResult pgraph_m2mf_execution_cpu_step(struct NV2AState *d,
    PGRAPHM2MFCommand *command, const PGRAPHM2MFExecution *execution,
    size_t budget);
/* Only the PUBLISH phase may write completion. Returns YIELD if notifier RAM
 * is still GPU-owned. This does not deliver an optional notify interrupt. */
PGRAPHM2MFResult pgraph_m2mf_execution_publish(struct NV2AState *d,
    PGRAPHM2MFCommand *command, const PGRAPHM2MFExecution *execution);
#endif

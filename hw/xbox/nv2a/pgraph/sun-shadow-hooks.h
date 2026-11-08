/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_SUN_SHADOW_HOOKS_H
#define NV2A_SUN_SHADOW_HOOKS_H
#include <stdbool.h>
#include <stdint.h>

#define PGRAPH_SUN_DRAW_UP_PC 0x001e7773u
#define PGRAPH_SUN_DRAW_INDEXED_PC 0x001e78e5u
typedef struct PgraphSunFifo PgraphSunFifo;
typedef struct CPUState CPUState;
typedef struct NV2AState NV2AState;
typedef struct PgraphVkSunNative PgraphVkSunNative;

/* Activation: emulation thread, BQL held, vCPU stopped, FIFO parked or its
 * lock held. Detach needs BQL/stopped vCPU; it does not mutate the queue.
 * Queue storage is lent to the CPU until detach. Its owner must also stop
 * the FIFO consumer and cancel resource capture before freeing that storage.
 * Activation authenticates loaded code and queues a translation flush before
 * guest execution resumes. Off emits no CPU helpers or normal draw hooks. */
extern bool xemu_sun_camera_enabled;
bool xemu_sun_camera_attach(PgraphSunFifo *fifo);
void xemu_sun_camera_detach(void);
void xemu_sun_camera_capture(CPUState *cpu, uint64_t pc, uint32_t cursor);
/* Emulation owner, BQL held. Uses the existing park handshake to change
 * worker dispatch; normal rendering never polls an enhancement flag. */
bool nv2a_sun_request(bool enabled, uint64_t lifetime);
bool pgraph_sun_worker_requested(void);
void pgraph_sun_worker_failed(void);
PgraphSunFifo *pgraph_sun_worker_enter(NV2AState *d);
void pgraph_sun_worker_leave(NV2AState *d);
void pfifo_loop_sun(NV2AState *d, PgraphSunFifo *fifo);
PgraphVkSunNative *pgraph_vk_sun_create(NV2AState *d, uint64_t lifetime);
PgraphSunFifo *pgraph_vk_sun_fifo(PgraphVkSunNative *sun);
bool pgraph_vk_sun_enter(NV2AState *d, PgraphVkSunNative *sun);
void pgraph_vk_sun_leave(NV2AState *d, PgraphVkSunNative *sun);
void pgraph_vk_sun_free(PgraphVkSunNative *sun);
#endif

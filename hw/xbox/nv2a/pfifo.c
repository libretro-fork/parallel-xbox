/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "nv2a_int.h"

typedef struct RAMHTEntry {
    uint32_t handle;
    hwaddr instance;
    enum FIFOEngine engine;
    unsigned int channel_id : 5;
    bool valid;
} RAMHTEntry;

static void pfifo_run_pusher(NV2AState *d);
static uint32_t ramht_hash(NV2AState *d, uint32_t handle);
static RAMHTEntry ramht_lookup(NV2AState *d, uint32_t handle);

/* PFIFO - MMIO and DMA FIFO submission to PGRAPH and VPE */
/* The guest's PFIFO registers, from the vCPU, with no lock: the interrupt
 * words are the vCPU's alone, the rest single words the pfifo thread
 * reads at its next method (after the kick a write gives) */
uint64_t pfifo_read(void *opaque, hwaddr addr, unsigned int size)
{
    NV2AState *d = (NV2AState *)opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PFIFO_INTR_0:
        r = d->pfifo.pending_interrupts;
        break;
    case NV_PFIFO_INTR_EN_0:
        r = d->pfifo.enabled_interrupts;
        break;
    case NV_PFIFO_RUNOUT_STATUS:
        r = NV_PFIFO_RUNOUT_STATUS_LOW_MARK; /* low mark empty */
        break;
    default:
        r = pfifo_reg_r(d, addr);
        break;
    }

    nv2a_reg_log_read(NV_PFIFO, addr, size, r);
    return r;
}

void pfifo_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    NV2AState *d = (NV2AState *)opaque;

    nv2a_reg_log_write(NV_PFIFO, addr, size, val);

    switch (addr) {
    case NV_PFIFO_INTR_0:
        d->pfifo.pending_interrupts &= ~val;
        nv2a_update_irq(d);
        break;
    case NV_PFIFO_INTR_EN_0:
        d->pfifo.enabled_interrupts = val;
        nv2a_update_irq(d);
        break;
    default:
        pfifo_reg_w(d, addr, (uint32_t)val);
        break;
    }

    pfifo_kick_guest(d);
}

/* Work the guest gave the GPU: counted first, then the kick that has the
 * thread take the count (pfifo_thread) */
void pfifo_kick_guest(NV2AState *d)
{
    retro_atomic_fetch_add_seq_cst_int(&d->pfifo.guest_kicks, 1);
    pfifo_kick(d);
}

/* From any thread, no lock: the flag, then the thread's eventcount. */
void pfifo_kick(NV2AState *d)
{
    retro_atomic_store_release_int(&d->pfifo.fifo_kick, 1);
    retro_eventcount_notify(&d->pfifo.fifo_ec);
}

static bool can_fifo_access(NV2AState *d) {
    return pgraph_reg_r(&d->pgraph, NV_PGRAPH_FIFO) &
           NV_PGRAPH_FIFO_ACCESS;
}

/* If NV097_FLIP_STALL was executed, check if the flip has completed.
 * This will usually happen in the VSYNC interrupt handler.
 */
static bool is_flip_stall_complete(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    /* The flip may not complete before the modelled render deadline, a
     * continuous one: the game's own vblank pacing supplies any grid. */
    if (xemu_flip_ready_ns &&
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME) < xemu_flip_ready_ns) {
        return false;
    }

    uint32_t s = pgraph_reg_r(pg, NV_PGRAPH_SURFACE);

    NV2A_DPRINTF("flip stall read: %d, write: %d, modulo: %d\n",
        GET_MASK(s, NV_PGRAPH_SURFACE_READ_3D),
        GET_MASK(s, NV_PGRAPH_SURFACE_WRITE_3D),
        GET_MASK(s, NV_PGRAPH_SURFACE_MODULO_3D));

    if (GET_MASK(s, NV_PGRAPH_SURFACE_READ_3D)
        != GET_MASK(s, NV_PGRAPH_SURFACE_WRITE_3D)) {
        return true;
    }

    return false;
}

/* in the pusher's batch */
static bool pfifo_stall_for_flip(NV2AState *d)
{
    bool should_stall = false;

    if (retro_atomic_load_relaxed_int(&d->pgraph.waiting_for_flip)) {
        if (!is_flip_stall_complete(d)) {
            should_stall = true;
        } else {
            retro_atomic_store_relaxed_int(&d->pgraph.waiting_for_flip, 0);
            /* The guest resumes submitting: the modelled GPU queue starts. */
            xemu_flip_completed_ns =
                qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        }
    }

    return should_stall;
}

static bool pfifo_puller_should_stall(NV2AState *d)
{
    return pfifo_stall_for_flip(d) ||
           pgraph_pending_bit(&d->pgraph, NV_PGRAPH_INTR_ERROR) ||
           pgraph_pending_bit(&d->pgraph, NV_PGRAPH_INTR_CONTEXT_SWITCH) ||
           !can_fifo_access(d);
}

/* The core's emulator thread waits with the guest's time stopped while the
 * GPU works (ui/libretro.c), and is woken when this thread parks */
retro_atomic_int_t nv2a_libretro_waiter;

/* Nothing to do. A pending report is written when the host GPU reaches it,
 * not when the guest next pushes: with one pending, wait for the GPU's own
 * completion of it, then serve it. */
static inline void pfifo_idle_wait(NV2AState *d)
{
    const PGRAPHRenderer *renderer = pgraph_renderer(&d->pgraph);

    if (d->pgraph.reports_pending && renderer &&
        renderer->ops.wait_pending_report) {
        bool waited;

        qemu_mutex_unlock(&d->pfifo.lock);
        waited = renderer->ops.wait_pending_report(d);
        qemu_mutex_lock(&d->pfifo.lock);
        if (waited) {
            return;
        }
    }
    /* The wait is prepared under the lock and committed without it: a
     * kick between the two (the flag set after the prepare) cancels it,
     * one before was seen by the check. One wait, as upstream's:
     * nv2a_lock_fifo wakes the thread without a kick to have it park for
     * the guest (park_requests). */
    if (!retro_atomic_load_acquire_int(&d->pfifo.fifo_kick)) {
        int key = retro_eventcount_prepare_wait(&d->pfifo.fifo_ec);
        if (retro_atomic_load_acquire_int(&d->pfifo.fifo_kick) ||
            retro_atomic_load_acquire_int(&d->pfifo.park_requests) ||
            retro_atomic_load_acquire_int(&d->exiting)) {
            retro_eventcount_cancel_wait(&d->pfifo.fifo_ec);
            return;
        }
        /* The frame's work done, where the thread would park: the display
         * drawn from what it left, if the frontend asked at its frame's end
         * (nv2a_libretro_frame_end), and the frontend told, a Dekker
         * handshake as below; then round again, to park */
        int end = retro_atomic_load_acquire_int(&d->pfifo.frame_end_req);
        if (end != retro_atomic_load_relaxed_int(&d->pfifo.frame_end_done)) {
            retro_eventcount_cancel_wait(&d->pfifo.fifo_ec);
            retro_atomic_store_relaxed_int(&d->pgraph.scanout_changed, 1);
            pgraph_process_pending(d);
            retro_atomic_store_release_int(&d->pfifo.frame_end_done, end);
            retro_atomic_thread_fence_seq_cst();
            if (retro_atomic_load_relaxed_int(&nv2a_libretro_waiter)) {
                qemu_notify_event();
            }
            return;
        }
        /* a Dekker handshake with the emulator thread's wait (ui/libretro.c
         * lr_run_frame, nv2a_libretro_switch_renderer): the guest's work
         * this pass took marked done, a full fence, then the look at the
         * waiter flag, which that thread stores and fences before it looks
         * (nv2a_libretro_guest_busy) */
        retro_atomic_store_release_int(&d->pfifo.guest_done,
                                       d->pfifo.guest_seen);
        retro_atomic_thread_fence_seq_cst();
        if (retro_atomic_load_relaxed_int(&nv2a_libretro_waiter)) {
            qemu_notify_event();
        }
        qemu_mutex_unlock(&d->pfifo.lock);
        retro_eventcount_commit_wait(&d->pfifo.fifo_ec, key);
        qemu_mutex_lock(&d->pfifo.lock);
    }
}

/* Both locks held, pfifo's then pgraph's: the pusher takes them for a
 * batch of methods (pfifo_run_pusher), where each method used to trade
 * one for the other and back. */
static ssize_t pfifo_run_puller(NV2AState *d, uint32_t method_entry,
                                uint32_t parameter, uint32_t *parameters,
                                size_t num_words_available,
                                size_t max_lookahead_words)
{
    if (pfifo_puller_should_stall(d)) {
        return -1;
    }
    ssize_t num_proc = -1;

    // TODO think more about locking

    if (!GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_PULL0), NV_PFIFO_CACHE1_PULL0_ACCESS) ||
        (pfifo_reg_r(d, NV_PFIFO_CACHE1_STATUS) & NV_PFIFO_CACHE1_STATUS_LOW_MARK)) {
        return -1;
    }

    uint32_t method = method_entry & 0x1FFC;
    uint32_t subchannel =
        GET_MASK(method_entry, NV_PFIFO_CACHE1_METHOD_SUBCHANNEL);
    bool inc = !GET_MASK(method_entry, NV_PFIFO_CACHE1_METHOD_TYPE);

    if (method == 0) {
        RAMHTEntry entry = ramht_lookup(d, parameter);
        assert(entry.valid);
        // assert(entry.channel_id == state->channel_id);
        assert(entry.engine == ENGINE_GRAPHICS);

        /* the engine is bound to the subchannel */
        assert(subchannel < 8);
        PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_ENGINE, 3 << (4*subchannel), entry.engine);
        PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_PULL1, NV_PFIFO_CACHE1_PULL1_ENGINE, entry.engine);

        // Switch contexts if necessary
        if (can_fifo_access(d)) {
            pgraph_context_switch(d, entry.channel_id);
            if (!pgraph_pending_bit(&d->pgraph,
                                    NV_PGRAPH_INTR_CONTEXT_SWITCH)) {
                num_proc =
                    pgraph_method(d, subchannel, 0, entry.instance, parameters,
                                  num_words_available, max_lookahead_words, inc);
            }
        }

    } else if (method >= 0x100) {
        // method passed to engine

        /* methods that take objects.
         * TODO: Check this range is correct for the nv2a */
        if (method >= 0x180 && method < 0x200) {
            //bql_lock();
            RAMHTEntry entry = ramht_lookup(d, parameter);
            assert(entry.valid);
            // assert(entry.channel_id == state->channel_id);
            parameter = entry.instance;
            //bql_unlock();
        }

        enum FIFOEngine engine = GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_ENGINE), 3 << (4*subchannel));
        assert(engine == ENGINE_GRAPHICS);
        PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_PULL1, NV_PFIFO_CACHE1_PULL1_ENGINE, engine);

        if (can_fifo_access(d)) {
            num_proc =
                pgraph_method(d, subchannel, method, parameter, parameters,
                              num_words_available, max_lookahead_words, inc);
        }
    } else {
        assert(!"Unrecognized pfifo puller method");
    }

    if (num_proc > 0) {
        pfifo_reg_w(d, NV_PFIFO_CACHE1_STATUS, pfifo_reg_r(d, NV_PFIFO_CACHE1_STATUS) | NV_PFIFO_CACHE1_STATUS_LOW_MARK);
    }

    return num_proc;
}

#define PFIFO_METHOD_BATCH 64

/* A guest thread (the vCPU, the display) about to take the fifo lock says
 * so, and the pusher ends its batch at the next method. */
void pfifo_guest_lock(NV2AState *d)
{
    retro_atomic_fetch_add_int(&d->pfifo.guest_waiting, 1);
    xemu_tbrate_lock(&d->pfifo.lock);
    retro_atomic_fetch_sub_int(&d->pfifo.guest_waiting, 1);
}

static bool pfifo_pusher_should_stall(NV2AState *d)
{
    return !can_fifo_access(d) ||
           pgraph_pending_bit(&d->pgraph, NV_PGRAPH_INTR_ERROR);
}

static void pfifo_run_pusher(NV2AState *d)
{

    if (!GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_PUSH0), NV_PFIFO_CACHE1_PUSH0_ACCESS) ||
        !GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_PUSH), NV_PFIFO_CACHE1_DMA_PUSH_ACCESS) ||
        GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_PUSH), NV_PFIFO_CACHE1_DMA_PUSH_STATUS)) {
        return;
    }

    unsigned int channel_id = GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_PUSH1),
                                       NV_PFIFO_CACHE1_PUSH1_CHID);


    /* Channel running DMA mode */
    uint32_t channel_modes = pfifo_reg_r(d, NV_PFIFO_MODE);
    assert(channel_modes & (1 << channel_id));

    assert(GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_PUSH1), NV_PFIFO_CACHE1_PUSH1_MODE)
            == NV_PFIFO_CACHE1_PUSH1_MODE_DMA);

    /* We're running so there should be no pending errors... */
    assert(GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_STATE), NV_PFIFO_CACHE1_DMA_STATE_ERROR)
            == NV_PFIFO_CACHE1_DMA_STATE_ERROR_NONE);

    hwaddr dma_instance =
        GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_INSTANCE),
                 NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS) << 4;

    hwaddr dma_len;
    uint8_t *dma = nv_dma_map(d, dma_instance, &dma_len);
    /* The methods run in batches under the FIFO's lock, let go between
     * batches and at once when a guest thread wants it (pfifo_guest_lock).
     * PGRAPH has no lock: the CPU's accesses to the surfaces' memory come
     * through PGRAPHCpuAccess, served here before each method. */
    unsigned batch = 0;
    PGRAPHState *pg = &d->pgraph;
    while (!pfifo_pusher_should_stall(d)) {
        if (++batch == PFIFO_METHOD_BATCH ||
            retro_atomic_load_acquire_int(&d->pfifo.guest_waiting)) {
            batch = 0;
            qemu_mutex_unlock(&d->pfifo.lock);
            qemu_mutex_lock(&d->pfifo.lock);
            /* nv_dma_map's mapping holds; the registers may have moved */
        }
        if ((retro_atomic_load_relaxed_int(&pg->cpu.touched) ||
             retro_atomic_load_relaxed_int(&pg->cpu.dl_pending)) &&
            pgraph_renderer(pg)->ops.cpu_access_service) {
            pgraph_renderer(pg)->ops.cpu_access_service(d);
        }
        uint32_t dma_get_v = pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_GET);
        /* the guest's, written without the lock (user_write) */
        uint32_t dma_put_v = (uint32_t)retro_atomic_load_acquire_int(
            &d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT]);

        if (dma_get_v == dma_put_v) break;
        if (dma_get_v >= dma_len) {
            assert(!"Dma value is out of range in PFIFO pusher");
            PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_ERROR,
                     NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION);
            break;
        }

        /* Words the pusher may read from GET: up to PUT, or, once PUT has
         * wrapped to the start of the ring, up to the end of the DMA
         * object, limit + 1 (the jump word there brings GET back). The unsigned
         * PUT - GET was a huge count on a wrapped ring, and the
         * DRAW_ARRAYS lookahead then took the previous lap's words past
         * the jump for more draws: GET left the object (Crazy Taxi at
         * 10x, the pusher a lap behind while the city loads). */
        uint32_t dma_end = (uint32_t)((dma_len + 1) & ~(hwaddr)3);
        size_t num_words_available =
            (dma_put_v >= dma_get_v ? dma_put_v : dma_end) - dma_get_v;
        assert(num_words_available % 4 == 0);
        num_words_available /= 4;

        uint32_t *word_ptr = (uint32_t*)(dma + dma_get_v);
        uint32_t word = ldl_le_p(word_ptr);
        dma_get_v += 4;

        uint32_t method_type =
            GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_STATE), NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE);
        uint32_t method_subchannel =
            GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_STATE), NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL);
        uint32_t method =
            GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_STATE), NV_PFIFO_CACHE1_DMA_STATE_METHOD) << 2;
        uint32_t method_count =
            GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_STATE), NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT);

        uint32_t subroutine_state =
            GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_SUBROUTINE), NV_PFIFO_CACHE1_DMA_SUBROUTINE_STATE);

        if (method_count) {
            /* data word of methods command */
            pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_DATA_SHADOW, word);

            assert((method & 3) == 0);
            uint32_t method_entry = 0;
            SET_MASK(method_entry, NV_PFIFO_CACHE1_METHOD_ADDRESS, method >> 2);
            SET_MASK(method_entry, NV_PFIFO_CACHE1_METHOD_TYPE, method_type);
            SET_MASK(method_entry, NV_PFIFO_CACHE1_METHOD_SUBCHANNEL,
                     method_subchannel);

            pfifo_reg_w(d, NV_PFIFO_CACHE1_STATUS, pfifo_reg_r(d, NV_PFIFO_CACHE1_STATUS) & ~NV_PFIFO_CACHE1_STATUS_LOW_MARK);

            ssize_t num_words_processed =
                pfifo_run_puller(d, method_entry, word, word_ptr,
                                 MIN(method_count, num_words_available),
                                 num_words_available);
            if (num_words_processed < 0) {
                break;
            }

            dma_get_v += (num_words_processed-1)*4;

            if (method_type == NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE_INC) {
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_METHOD,
                         (method + 4*num_words_processed) >> 2);
            }
            PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT,
                     method_count - MIN(method_count, num_words_processed));

            pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_DCOUNT, pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_DCOUNT) + num_words_processed);
        } else {
            /* no command active - this is the first word of a new one */
            pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_RSVD_SHADOW, word);

            /* match all forms */
            if ((word & 0xe0000003) == 0x20000000) {
                /* old jump */
                pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW, dma_get_v);
                dma_get_v = word & 0x1fffffff;
                NV2A_DPRINTF("pb OLD_JMP 0x%x\n", dma_get_v);
            } else if ((word & 3) == 1) {
                /* jump */
                pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_GET_JMP_SHADOW, dma_get_v);
                dma_get_v = word & 0xfffffffc;
                NV2A_DPRINTF("pb JMP 0x%x\n", dma_get_v);
            } else if ((word & 3) == 2) {
                /* call */
                if (subroutine_state) {
                    PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_ERROR,
                             NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL);
                    break;
                } else {
                    pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_SUBROUTINE, dma_get_v);
                    PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_SUBROUTINE,
                             NV_PFIFO_CACHE1_DMA_SUBROUTINE_STATE, 1);
                    dma_get_v = word & 0xfffffffc;
                    NV2A_DPRINTF("pb CALL 0x%x\n", dma_get_v);
                }
            } else if (word == 0x00020000) {
                /* return */
                if (!subroutine_state) {
                    PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_ERROR,
                             NV_PFIFO_CACHE1_DMA_STATE_ERROR_RETURN);
                    // break;
                } else {
                    dma_get_v = pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_SUBROUTINE) & 0xfffffffc;
                    PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_SUBROUTINE,
                             NV_PFIFO_CACHE1_DMA_SUBROUTINE_STATE, 0);
                    NV2A_DPRINTF("pb RET 0x%x\n", dma_get_v);
                }
            } else if ((word & 0xe0030003) == 0) {
                /* increasing methods */
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_METHOD,
                         (word & 0x1fff) >> 2 );
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL,
                         (word >> 13) & 7);
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT,
                         (word >> 18) & 0x7ff);
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE,
                         NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE_INC);
                pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_DCOUNT, 0);
            } else if ((word & 0xe0030003) == 0x40000000) {
                /* non-increasing methods */
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_METHOD,
                         (word & 0x1fff) >> 2 );
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL,
                         (word >> 13) & 7);
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT,
                         (word >> 18) & 0x7ff);
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE,
                         NV_PFIFO_CACHE1_DMA_STATE_METHOD_TYPE_NON_INC);
                pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_DCOUNT, 0);
            } else {
                NV2A_DPRINTF("pb reserved cmd 0x%x - 0x%x\n",
                             dma_get_v, word);
                PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_STATE, NV_PFIFO_CACHE1_DMA_STATE_ERROR,
                         NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD);
                // break;
                assert(!"Reserved pb command - invalid GPU command. Check logs for more info");
            }
        }

        /* read by the guest, no lock */
        pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_GET, dma_get_v);

        if (GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_STATE), NV_PFIFO_CACHE1_DMA_STATE_ERROR)) {
            break;
        }
    }

    // NV2A_DPRINTF("DMA pusher done: max 0x%" HWADDR_PRIx ", 0x%" HWADDR_PRIx " - 0x%" HWADDR_PRIx "\n",
    //      dma_len, control->dma_get, control->dma_put);

    uint32_t error = GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_STATE), NV_PFIFO_CACHE1_DMA_STATE_ERROR);
    if (error) {
        NV2A_DPRINTF("pb error: %d\n", error);
        assert(!"Error reported in PFIFO dma state - check logs for more info");

        PFIFO_REG_SET_MASK(d, NV_PFIFO_CACHE1_DMA_PUSH, NV_PFIFO_CACHE1_DMA_PUSH_STATUS, 1); /* suspended */

        // d->pfifo.pending_interrupts |= NV_PFIFO_INTR_0_DMA_PUSHER;
        // nv2a_update_irq(d);
    }
}

/* The emulator thread asked for the GPU (nv2a_lock_fifo): parked here,
 * between two batches, with nothing of the FIFO's or PGRAPH's under way,
 * until its requests are withdrawn; kicks meanwhile stay flagged for the
 * loop after. The lock is let go for the sleep, as for the idle wait. */
static void pfifo_park_for_guest(NV2AState *d)
{
    retro_atomic_store_release_int(&d->pfifo.parked_for_guest, 1);
    retro_eventcount_notify(&d->pfifo.parked_ec);
    qemu_mutex_unlock(&d->pfifo.lock);
    for (;;) {
        int key = retro_eventcount_prepare_wait(&d->pfifo.fifo_ec);
        if (!retro_atomic_load_acquire_int(&d->pfifo.park_requests) ||
            retro_atomic_load_acquire_int(&d->exiting)) {
            retro_eventcount_cancel_wait(&d->pfifo.fifo_ec);
            break;
        }
        retro_eventcount_commit_wait(&d->pfifo.fifo_ec, key);
    }
    qemu_mutex_lock(&d->pfifo.lock);
    retro_atomic_store_release_int(&d->pfifo.parked_for_guest, 0);
}

void *pfifo_thread(void *arg)
{
    NV2AState *d = (NV2AState *)arg;

    pgraph_init_thread(d);

    rcu_register_thread();

    qemu_mutex_lock(&d->pfifo.lock);
    retro_atomic_store_release_int(&d->pfifo.thread_running, 1);
    while (true) {
        if (retro_atomic_load_acquire_int(&d->pfifo.park_requests)) {
            pfifo_park_for_guest(d);
            if (retro_atomic_load_acquire_int(&d->exiting)) {
                break;
            }
            continue;
        }
        /* the kick taken, and with it the guest's count: a kick that
         * read-modify-write reads carries the count its guest kick
         * stored first (pfifo_kick_guest); one it misses is seen before
         * the thread parks */
        retro_atomic_exchange_int(&d->pfifo.fifo_kick, 0);
        d->pfifo.guest_seen =
            retro_atomic_load_acquire_int(&d->pfifo.guest_kicks);

        pgraph_process_pending(d);

        if (!retro_atomic_load_acquire_int(&d->pfifo.halt)) {
            pfifo_run_pusher(d);
        }

        pgraph_process_pending_reports(d);

        if (!retro_atomic_load_acquire_int(&d->pfifo.fifo_kick)) {
            // Both the pusher and puller are waiting for some action
            pfifo_idle_wait(d);
        }

        if (retro_atomic_load_acquire_int(&d->exiting)) {
            break;
        }
    }
    qemu_mutex_unlock(&d->pfifo.lock);

    rcu_unregister_thread();

    return NULL;
}

static uint32_t ramht_hash(NV2AState *d, uint32_t handle)
{
    unsigned int ramht_size =
        1 << (GET_MASK(pfifo_reg_r(d, NV_PFIFO_RAMHT), NV_PFIFO_RAMHT_SIZE)+12);

    /* XXX: Think this is different to what nouveau calculates... */
    unsigned int bits = ctz32(ramht_size)-1;

    uint32_t hash = 0;
    while (handle) {
        hash ^= (handle & ((1 << bits) - 1));
        handle >>= bits;
    }

    unsigned int channel_id = GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_PUSH1),
                                       NV_PFIFO_CACHE1_PUSH1_CHID);
    hash ^= channel_id << (bits - 4);

    return hash;
}


static RAMHTEntry ramht_lookup(NV2AState *d, uint32_t handle)
{
    hwaddr ramht_size =
        1 << (GET_MASK(pfifo_reg_r(d, NV_PFIFO_RAMHT), NV_PFIFO_RAMHT_SIZE)+12);

    uint32_t hash = ramht_hash(d, handle);
    assert(hash * 8 < ramht_size);

    hwaddr ramht_address =
        GET_MASK(pfifo_reg_r(d, NV_PFIFO_RAMHT),
                 NV_PFIFO_RAMHT_BASE_ADDRESS) << 12;

    assert(ramht_address + hash * 8 < memory_region_size(&d->ramin));

    uint8_t *entry_ptr = d->ramin_ptr + ramht_address + hash * 8;

    uint32_t entry_handle = ldl_le_p((uint32_t*)entry_ptr);
    uint32_t entry_context = ldl_le_p((uint32_t*)(entry_ptr + 4));

    return (RAMHTEntry){
        .handle = entry_handle,
        .instance = (entry_context & NV_RAMHT_INSTANCE) << 4,
        .engine = (entry_context & NV_RAMHT_ENGINE) >> 16,
        .channel_id = (entry_context & NV_RAMHT_CHID) >> 24,
        .valid = entry_context & NV_RAMHT_STATUS,
    };
}

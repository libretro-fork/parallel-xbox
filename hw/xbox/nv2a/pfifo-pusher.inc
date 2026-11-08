/* SPDX-License-Identifier: LGPL-2.0-or-later */
/* Shared pusher implementation. Specialize at compile time so normal FIFO
 * dispatch contains no enhancement checks, callback or lookahead change. */
#if PFIFO_SUN_CAPTURE
void pfifo_run_pusher_sun(NV2AState *d, PgraphSunFifo *sun)
#else
static void pfifo_run_pusher(NV2AState *d)
#endif
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

#if PFIFO_SUN_CAPTURE
            pgraph_sun_fifo_prepare(sun, (uint32_t)(dma - d->vram_ptr) + dma_get_v - 4,
                                    method_subchannel, method, word);
            /* BEGIN/END runs are observed one parameter at a time. Other
             * method payloads may batch, but cannot look ahead across BEGIN. */
            size_t available = MIN(method_count, num_words_available);
            if (method == PGRAPH_SUN_FIFO_BEGIN_END) available = MIN(available, 1);
            ssize_t num_words_processed =
                pfifo_run_puller(d, method_entry, word, word_ptr, available, 0);
            pgraph_sun_fifo_complete(sun, method, word, num_words_processed > 0);
#else
            ssize_t num_words_processed =
                pfifo_run_puller(d, method_entry, word, word_ptr,
                                 MIN(method_count, num_words_available),
                                 num_words_available);
#endif
            /* A deferred method may consume no words. Leave GET and the
             * method count untouched and yield to the owner loop; its
             * completion path must kick the FIFO before it can be retried. */
            if (num_words_processed <= 0) {
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

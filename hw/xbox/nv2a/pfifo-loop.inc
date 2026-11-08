/* SPDX-License-Identifier: LGPL-2.0-or-later */
#if PFIFO_SUN_CAPTURE
void pfifo_loop_sun(NV2AState *d, PgraphSunFifo *sun)
#else
static void pfifo_loop_normal(NV2AState *d)
#endif
{
    while (true) {
        if (retro_atomic_load_acquire_int(&d->pfifo.park_requests)) {
            pfifo_park_for_guest(d);
            if (retro_atomic_load_acquire_int(&d->exiting)) {
                break;
            }
            if (pgraph_sun_worker_requested() != !!PFIFO_SUN_CAPTURE) break;
            continue;
        }
        /* the kick taken, and with it the guest's count: a kick that
         * read-modify-write reads carries the count its guest kick
         * stored first (pfifo_kick_guest); one it misses is seen before
         * the thread parks */
        retro_atomic_exchange_int(&d->pfifo.fifo_kick, 0);
        d->pfifo.guest_seen =
            retro_atomic_load_acquire_int(&d->pfifo.guest_kicks);

        lr_dsp_bus_borrow();
        pgraph_process_pending(d);
#if PFIFO_SUN_CAPTURE
        if (!pgraph_sun_worker_requested()) { lr_dsp_bus_return(); break; }
#endif

        if (!retro_atomic_load_acquire_int(&d->pfifo.halt)) {
#if PFIFO_SUN_CAPTURE
            pfifo_run_pusher_sun(d, sun);
#else
            pfifo_run_pusher(d);
#endif
        }

        pgraph_process_pending_reports(d);
        lr_dsp_bus_return();

        if (!retro_atomic_load_acquire_int(&d->pfifo.fifo_kick)) {
            // Both the pusher and puller are waiting for some action
            pfifo_idle_wait(d);
        }

        if (retro_atomic_load_acquire_int(&d->exiting)) {
            break;
        }
    }
}

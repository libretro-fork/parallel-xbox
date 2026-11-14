/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2021 Matt Borgerson
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

/* USER - PFIFO MMIO and DMA submission area */
/* The channel's DMA registers, the guest's end of the pushbuffer: no lock.
 * PUT and REF are the guest's to write, GET the pusher's; each is one
 * word read and written atomically, and a PUT write kicks the thread. The
 * channel's mode and id are set up once, at boot (pfifo_write, under the
 * lock), before the first of these. */
uint64_t user_read(void *opaque, hwaddr addr, unsigned int size)
{
    (void)size;
    NV2AState *d = (NV2AState *)opaque;

    unsigned int channel_id = addr >> 16;
    if (channel_id >= NV2A_NUM_CHANNELS) {

        return 0;
    }

    uint32_t channel_modes = pfifo_reg_r(d, NV_PFIFO_MODE);

    uint64_t r = 0;
    if (channel_modes & (1u << channel_id)) {
        /* DMA Mode */

        unsigned int cur_channel_id =
            GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_PUSH1),
                     NV_PFIFO_CACHE1_PUSH1_CHID);

        if (channel_id == cur_channel_id) {
            switch (addr & 0xFFFF) {
            case NV_USER_DMA_PUT:
                r = pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_PUT);
                break;
            case NV_USER_DMA_GET:
                r = pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_GET);
                /* A game waiting for room in its pushbuffer reads this
                 * again and again while the GPU works through it: it
                 * changes at every read, but the loop is a wait on the
                 * GPU's thread (the idle detection's, accel/tcg/cputlb.c
                 * do_ld_mmio_beN), so its turns stop taking a host core
                 * from the GPU's thread */
                if (current_cpu &&
                    r != pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_PUT)) {
                    current_cpu->lr_poll_any = true;
                }
                break;
            case NV_USER_REF:
                r = pfifo_reg_r(d, NV_PFIFO_CACHE1_REF);
                break;
            default:
                break;
            }
        } else {
            /* Inactive RAMFC channels are not implemented; read as zero. */
        }
    } else {
        /* PIO mode is not implemented; read as zero. */
    }


    return r;
}

void user_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    (void)size;
    NV2AState *d = (NV2AState *)opaque;



    unsigned int channel_id = addr >> 16;
    if (channel_id >= NV2A_NUM_CHANNELS) {
        return;
    }

    uint32_t channel_modes = pfifo_reg_r(d, NV_PFIFO_MODE);
    if (channel_modes & (1u << channel_id)) {
        /* DMA Mode */
        unsigned int cur_channel_id =
            GET_MASK(pfifo_reg_r(d, NV_PFIFO_CACHE1_PUSH1),
                     NV_PFIFO_CACHE1_PUSH1_CHID);

        if (channel_id == cur_channel_id) {
            switch (addr & 0xFFFF) {
            case NV_USER_DMA_PUT:
                /* the words up to PUT were written before: the pusher
                 * takes PUT with an acquire load */
                retro_atomic_store_release_int(
                    &d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT], (int)(uint32_t)val);
                break;
            case NV_USER_DMA_GET:
                pfifo_reg_w(d, NV_PFIFO_CACHE1_DMA_GET, (uint32_t)val);
                break;
            case NV_USER_REF:
                pfifo_reg_w(d, NV_PFIFO_CACHE1_REF, (uint32_t)val);
                break;
            default:
                /* Unsupported register: do not publish work to PFIFO. */
                return;
            }

            pfifo_kick_guest(d);

        } else {
            /* Inactive RAMFC channels are not implemented. */
        }
    } else {
        /* PIO mode is not implemented. */
    }
}

/*
 * MCPX DSP DMA
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2020-2021 Matt Borgerson
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

#include "qemu/osdep.h"
#include "qemu/compiler.h"
#include "debug.h"
#include "dsp_dma.h"
#include "dsp_dma_regs.h"
#include "interp/dsp_cpu_regs.h"

#ifdef DEBUG

const char *buffer_names[] = {
    "fifo0",            /* 0x0 */
    "fifo1",            /* 0x1 */
    "fifo2",            /* 0x2 */
    "fifo3",            /* 0x3 */
    "<unknown-0x4>",    /* 0x4 */
    "<unknown-0x5>",    /* 0x5 */
    "<unknown-0x6>",    /* 0x6 */
    "<unknown-0x7>",    /* 0x7 */
    "<unknown-0x8>",    /* 0x8 */
    "<unknown-0x9>",    /* 0x9 */
    "<unknown-0xa>",    /* 0xA */
    "<unknown-0xb>",    /* 0xB */
    "<unknown-0xc>",    /* 0xC */
    "<unknown-0xd>",    /* 0xD */
    "scratch-circular", /* 0xE */
    "scratch"           /* 0xF */
};

const char *format_names[] = {
    "8 bit",         /* 0x0 */
    "16 bit",        /* 0x1 */
    "24 bit msb",    /* 0x2 */
    "32 bit",        /* 0x3 */
    "<invalid-0x4>", /* 0x4 */
    "<invalid-0x5>", /* 0x5 */
    "24 bit lsb",    /* 0x6 */
    "<invalid-0x7>"  /* 0x7 */
};

const char *space_names[] = {
    "x", /* DSP_SPACE_X, 0x0 */
    "y", /* DSP_SPACE_Y, 0x1 */
    "p"  /* DSP_SPACE_P, 0x2 */
};

#endif

/* A transfer under way: the DSP's words it is at, and how each becomes
 * host bytes (or the other way), run by run of the host's memory */
typedef struct DmaTransfer {
    DSPDMAState *s;
    int space;
    uint32_t addr;          /* the DSP address of item 0 */
    uint32_t *mem;          /* the words in place, or NULL: by callbacks */
    unsigned int item_size;
    uint32_t item_mask;
    bool to_host;
    bool interleave;        /* item n is channel n % channels, block n / channels */
    uint32_t channels;
    uint32_t blocks;
    uint32_t n;             /* items done */
} DmaTransfer;

static inline uint32_t dma_item_index(const DmaTransfer *t, uint32_t n)
{
    return t->interleave ? (n % t->channels) * t->blocks + n / t->channels : n;
}

static inline uint32_t dma_word_read(const DmaTransfer *t, uint32_t n)
{
    uint32_t idx = dma_item_index(t, n);
    return t->mem ? t->mem[idx] : t->s->mem_read(t->s->mem_opaque, t->space, t->addr + idx);
}

static inline void dma_word_write(const DmaTransfer *t, uint32_t n, uint32_t v)
{
    uint32_t idx = dma_item_index(t, n);
    if (t->mem) {
        t->mem[idx] = v;
    } else {
        t->s->mem_write(t->s->mem_opaque, t->space, t->addr + idx, v);
    }
}

/* one run of the host's memory: the items in it converted */
static void dma_run(void *user, uint8_t *host, size_t bytes)
{
    DmaTransfer *t = user;
    size_t items = bytes / t->item_size;

    assert(bytes % t->item_size == 0);
    if (t->to_host) {
        if (t->item_size == 2) {
            for (size_t i = 0; i < items; i++) {
                uint16_t v = dma_word_read(t, t->n + i) >> 8;
                memcpy(host + i * 2, &v, 2);
            }
        } else {
            for (size_t i = 0; i < items; i++) {
                uint32_t v = dma_word_read(t, t->n + i);
                memcpy(host + i * 4, &v, 4);
            }
        }
    } else {
        if (t->item_size == 2) {
            for (size_t i = 0; i < items; i++) {
                uint16_t v;
                memcpy(&v, host + i * 2, 2);
                dma_word_write(t, t->n + i, (uint32_t)v << 8);
            }
        } else {
            for (size_t i = 0; i < items; i++) {
                uint32_t v;
                memcpy(&v, host + i * 4, 4);
                dma_word_write(t, t->n + i, v & t->item_mask);
            }
        }
    }
    t->n += items;
}

/* the scratch space as a ring: the transfer walked over it, wrapping */
static void scratch_circular_walk(
    DSPDMAState *s,
    uint32_t     scratch_base,
    uint32_t    *scratch_offset,
    uint32_t     scratch_size,
    uint32_t     transfer_size,
    bool         direction,
    DmaTransfer *t)
{
    if (*scratch_offset >= scratch_size) {
        // fprintf(stderr, "Initial scratch offset exceeds scratch size! Wrapping\n");
        *scratch_offset = 0;
    }

    while (transfer_size > 0) {
        size_t bytes_until_wrap = scratch_size - *scratch_offset;
        size_t chunk_size = MIN(transfer_size, bytes_until_wrap);
        uint32_t scratch_addr = scratch_base + *scratch_offset;

        s->scratch_walk(s->rw_opaque, scratch_addr, chunk_size, direction, dma_run, t);

        // Advance scratch pointer, wrap if we've reached the end
        *scratch_offset += chunk_size;
        if (*scratch_offset >= scratch_size) {
            *scratch_offset = 0;
        }

        transfer_size -= chunk_size;
    }
}

static void dsp_dma_run(DSPDMAState *s)
{
    if (!(s->control & DMA_CONTROL_RUNNING)
        || (s->control & DMA_CONTROL_FROZEN)) {
        return;
    }

    while (!(s->next_block & NODE_POINTER_EOL)) {
        uint32_t addr = s->next_block & NODE_POINTER_VAL;
        uint32_t block_addr = 0;
        int block_space = DSP_SPACE_X;

        if (addr < 0x1800) {
            assert(addr+6 < 0x1800);
            block_space = DSP_SPACE_X;
            block_addr = addr;
        } else if (addr >= 0x1800 && addr < 0x2000) { //?
            assert(addr+6 < 0x2000);
            block_space = DSP_SPACE_Y;
            block_addr = addr - 0x1800;
        } else if (addr >= 0x2800 && addr < 0x3800) { //?
            assert(addr+6 < 0x3800);
            block_space = DSP_SPACE_P;
            block_addr = addr - 0x2800;
        } else {
            assert(!"Dsp dma space address out of range");
        }

        uint32_t next_block = s->mem_read(s->mem_opaque, block_space, block_addr);
        uint32_t control = s->mem_read(s->mem_opaque, block_space, block_addr+1);
        uint32_t count = s->mem_read(s->mem_opaque, block_space, block_addr+2);

        uint32_t dsp_offset = s->mem_read(s->mem_opaque, block_space, block_addr+3);
        uint32_t scratch_offset = s->mem_read(s->mem_opaque, block_space, block_addr+4);
        uint32_t scratch_base = s->mem_read(s->mem_opaque, block_space, block_addr+5);
        uint32_t scratch_size = s->mem_read(s->mem_opaque, block_space, block_addr+6)+1;

        s->next_block = next_block;
        if (s->next_block & NODE_POINTER_EOL) {
            s->eol = true;
        }

        /* Decode control word */
        bool     dsp_interleave          = (control >> 0) & 1;
        bool     direction               = control & NODE_CONTROL_DIRECTION;
        uint32_t unk2                    = (control >>  2) & 0x3;
        bool     buffer_offset_writeback = (control >>  4) & 1;
        uint32_t buf_id                  = (control >>  5) & 0xf;
        // bool     unk9                    = (control >>  9) & 1; /* FIXME: What does this do? */
        uint32_t format                  = (control >> 10) & 0x7;
        bool     unk13                   = (control >> 13) & 1;
        // uint32_t dsp_step                = (control >> 14) & 0x3FF; // FIXME

        /* Check for unhandled control settings */
        assert(unk2 == 0x0);
        assert(unk13 == false);

        /* Decode count for interleaved mode */
        uint32_t channel_count = (count & 0xF) + 1;
        uint32_t block_count = count >> 4;

        unsigned int item_size = 4;
        uint32_t item_mask = 0xffffffff;
        // bool lsb = (format == 6); // FIXME

        switch(format) {
        case 0:
            item_size = 1;
            item_mask = 0x000000ff;
            break;
        case 1:
            item_size = 2;
            item_mask = 0x0000ffff;
            break;
        case 2:
        case 6:
            item_size = 4;
            item_mask = 0x00ffffff;
            break;
        default:
            fprintf(stderr, "Unknown dsp dma format: 0x%x\n", format);
            assert(!"Unknown dsp dma format");
            break;
        }

        size_t scratch_addr = scratch_base + scratch_offset;
        uint32_t mem_address = 0;
        int mem_space = DSP_SPACE_X;

        if (dsp_offset < 0x1800) {
            assert(dsp_offset+count < 0x1800);
            mem_space = DSP_SPACE_X;
            mem_address = dsp_offset;
        } else if (dsp_offset >= 0x1800 && dsp_offset < 0x2000) { //?
            assert(dsp_offset+count < 0x2000);
            mem_space = DSP_SPACE_Y;
            mem_address = dsp_offset - 0x1800;
        } else if (dsp_offset >= 0x2800 && dsp_offset < 0x3800) { //?
            assert(dsp_offset+count < 0x3800);
            mem_space = DSP_SPACE_P;
            mem_address = dsp_offset - 0x2800;
        } else {
            fprintf(stderr, "Attempt to access %08x\n", dsp_offset);
            assert(!"Dsp dma offset out of range");
        }

        size_t transfer_size = count * item_size;
        DmaTransfer t = {
            .s = s,
            .space = mem_space,
            .addr = mem_address,
            .item_size = item_size,
            .item_mask = item_mask,
            .to_host = direction,
            .interleave = direction && dsp_interleave,
            .channels = channel_count,
            .blocks = block_count,
        };
        uint32_t words = count;

        assert(item_size == 2 || item_size == 4);
        if (t.interleave) {
            // FIXME: Above xfer size calculation instead of
            // overwriting here
            transfer_size = block_count * item_size * channel_count;
            words = block_count * channel_count;
        }
        /* the DSP's words in place where they are plain memory; a
         * write to P goes through the callback (its code is dropped) */
        t.mem = s->mem_ptr(s->mem_opaque, mem_space, mem_address, words);

        if (direction) {
            switch (buf_id) {
            case 0x0:
            case 0x1:
            case 0x2:
            case 0x3:
                s->fifo_walk(s->rw_opaque, buf_id, transfer_size, true, dma_run, &t);
                break;
            case 0xE:
                scratch_circular_walk(s, scratch_base, &scratch_offset, scratch_size, transfer_size, true, &t);
                break;
            case 0xF:
                s->scratch_walk(s->rw_opaque, scratch_addr, transfer_size, true, dma_run, &t);
                break;
            default:
                fprintf(stderr, "Unknown DSP DMA buffer: 0x%x\n", buf_id);
                assert(!"Unknown dsp dma buffer");
                break;
            }
        } else {
            assert(!dsp_interleave);

            if (buf_id == 0xe) {
                scratch_circular_walk(s, scratch_base, &scratch_offset, scratch_size, transfer_size, false, &t);
            } else if (buf_id == 0xf) {
                s->scratch_walk(s->rw_opaque, scratch_addr, transfer_size, false, dma_run, &t);
            } else {
                fprintf(stderr, "Unhandled DSP DMA buffer: 0x%x\n", buf_id);
                assert(!"Unhandled dsp dma buffer");
            }
        }

        if (buffer_offset_writeback) {
            s->mem_write(s->mem_opaque, block_space, block_addr+4, scratch_offset);
        }

    }
}

uint32_t dsp_dma_read(DSPDMAState *s, DSPDMARegister reg)
{
    switch (reg) {
    case DMA_CONFIGURATION:
        return s->configuration;
    case DMA_CONTROL:
        if (s->control & DMA_CONTROL_RUNNING) {
            s->dma_read_count++;
            if (s->dma_read_count > 2) {
                s->control &= ~DMA_CONTROL_RUNNING;
                s->control |= DMA_CONTROL_STOPPED;
                s->dma_read_count = 0;
            }
        }
        return s->control;
    case DMA_START_BLOCK:
        return s->start_block;
    case DMA_NEXT_BLOCK:
        return s->next_block;
    default:
        assert(!"Invalid register for dsp_dma_read");
    }
    return 0;
}

void dsp_dma_write(DSPDMAState *s, DSPDMARegister reg, uint32_t v)
{
    switch (reg) {
    case DMA_CONFIGURATION:
        s->configuration = v;
        break;
    case DMA_CONTROL:
        switch(v & DMA_CONTROL_ACTION) {
        case DMA_CONTROL_ACTION_START:
            s->control |= DMA_CONTROL_RUNNING;
            s->control &= ~DMA_CONTROL_STOPPED;
            s->dma_read_count = 0;
            break;
        case DMA_CONTROL_ACTION_STOP:
            s->control |= DMA_CONTROL_STOPPED;
            s->control &= ~DMA_CONTROL_RUNNING;
            break;
        case DMA_CONTROL_ACTION_FREEZE:
            s->control |= DMA_CONTROL_FROZEN;
            break;
        case DMA_CONTROL_ACTION_UNFREEZE:
            s->control &= ~DMA_CONTROL_FROZEN;
            break;
        default:
            assert(!"Invalid DMA_CONTROL action");
            break;
        }
        dsp_dma_run(s);

        // IDK about this, but need to stop somehow?
        // s->control |= DMA_CONTROL_STOPPED;
        break;
    case DMA_START_BLOCK:
        s->start_block = v;
        break;
    case DMA_NEXT_BLOCK:
        s->next_block = v;
        break;
    default:
        assert(!"Invalid dma write register");
    }
}


/*
 * MCPX DSP DMA
 *
 * Copyright (c) 2015 espes
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

#ifndef DSP_DMA_H
#define DSP_DMA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* The host's memory a transfer goes to or from, a contiguous run at a
 * time: the scratch space (scatter-gather pages) or a FIFO (a ring of
 * them) walked over len bytes from addr (the FIFO's cursor), fn called
 * with each run in order, to be read (dir false) or written (true). The
 * DMA converts the DSP's words straight into or out of the run, with no
 * buffer in between. */
typedef void (*dsp_dma_run_func)(void *user, uint8_t *host, size_t bytes);
typedef void (*dsp_scratch_walk_func)(
    void *opaque, uint32_t addr, size_t len, bool dir,
    dsp_dma_run_func fn, void *user);
typedef void (*dsp_fifo_walk_func)(
    void *opaque, unsigned int index, size_t len, bool dir,
    dsp_dma_run_func fn, void *user);

#define DMA_CONTROL_RUNNING (1 << 4)
#define DMA_CONTROL_STOPPED (1 << 5)

typedef enum DSPDMARegister {
    DMA_CONFIGURATION,
    DMA_CONTROL,
    DMA_START_BLOCK,
    DMA_NEXT_BLOCK,
} DSPDMARegister;

/* Memory access callbacks for backend-agnostic DMA */
typedef uint32_t (*dsp_dma_mem_read_func)(void *opaque, int space, uint32_t addr);
typedef void (*dsp_dma_mem_write_func)(void *opaque, int space, uint32_t addr, uint32_t value);
/* the count words from addr in place, when they are plain memory the
 * DMA may read and write directly; NULL otherwise */
typedef uint32_t *(*dsp_dma_mem_ptr_func)(void *opaque, int space, uint32_t addr, uint32_t count);

typedef struct DSPDMAState {
    /* DSP memory access (backend-agnostic) */
    void *mem_opaque;
    dsp_dma_mem_read_func mem_read;
    dsp_dma_mem_write_func mem_write;
    dsp_dma_mem_ptr_func mem_ptr;

    /* System memory access */
    void *rw_opaque;
    dsp_scratch_walk_func scratch_walk;
    dsp_fifo_walk_func fifo_walk;

    uint32_t configuration;
    uint32_t control;
    uint32_t start_block;
    uint32_t next_block;

    bool error;
    bool eol;

    /* DMA completion timer: counts reads of DMA_CONTROL while RUNNING.
     * After 3 reads, transitions RUNNING -> STOPPED. */
    uint32_t dma_read_count;
} DSPDMAState;

uint32_t dsp_dma_read(DSPDMAState *s, DSPDMARegister reg);
void dsp_dma_write(DSPDMAState *s, DSPDMARegister reg, uint32_t v);

#endif

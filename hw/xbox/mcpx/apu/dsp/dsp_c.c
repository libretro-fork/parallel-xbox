/*
 * MCPX DSP emulator - C interpreter backend
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2020-2025 Matt Borgerson
 *
 * Adapted from Hatari DSP M56001 emulation
 * (C) 2001-2008 ARAnyM developer team
 * Adaption to Hatari (C) 2008 by Thomas Huth
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "qemu/osdep.h"
#include "dsp_internal.h"
#include "interp/dsp_cpu.h"
#include "debug.h"
#include "jit/dspjit.h"

static dsp_core_t *c_core(DSPState *dsp)
{
    return (dsp_core_t *)dsp->backend;
}

static uint32_t c_dma_mem_read(void *opaque, int space, uint32_t addr)
{
    return dsp56k_read_memory((dsp_core_t *)opaque, space, addr);
}

static void c_dma_mem_write(void *opaque, int space, uint32_t addr,
                            uint32_t value)
{
    dsp56k_write_memory((dsp_core_t *)opaque, space, addr, value);
}

static uint32_t *c_dma_mem_ptr(void *opaque, int space, uint32_t addr,
                               uint32_t count, bool write)
{
#if HOST_BIG_ENDIAN
    /* Program words use little-endian storage; retain byte-order-aware
     * callbacks on hosts where a raw word pointer would change the result. */
    if (space == DSP_SPACE_P) return NULL;
#endif
    uint32_t *mem = dsp56k_memory_ptr((dsp_core_t *)opaque, space, addr, count, write);
    /* P writes must invalidate code through the callback. For reads, retain
     * read_memory_p's rejection of uninitialized/out-of-width words: fall
     * back to ordered callbacks rather than bypassing their strict check. */
    if (mem && space == DSP_SPACE_P) {
        /* A reduction avoids a branch per valid program word and permits
         * vectorization. Invalid spans still use the strict read callbacks. */
        uint32_t a = 0, b = 0, c = 0, d = 0, i = 0;
        for (; count - i >= 4; i += 4) {
            a |= mem[i];
            b |= mem[i + 1];
            c |= mem[i + 2];
            d |= mem[i + 3];
        }
        uint32_t combined = a | b | c | d;
        for (; i < count; i++) combined |= mem[i];
        if (combined & 0xff000000) return NULL;
    }
    return mem;
}

static void c_dma_mem_descriptor(void *opaque, int space, uint32_t addr,
                                  DSPDMADescriptor *out)
{
    dsp_core_t *core = opaque;
    const uint32_t *words = c_dma_mem_ptr(opaque, space, addr, DSP_DMA_DESC_WORDS, false);
    if (words) {
        for (unsigned i = 0; i < DSP_DMA_DESC_WORDS; i++) out->words[i] = words[i];
    } else {
        /* Same ordered memory semantics, without per-word DMA indirection. */
        dsp56k_read_memory_words(core, space, addr, out->words, DSP_DMA_DESC_WORDS);
    }
}

static uint32_t c_read_peripheral(dsp_core_t *core, uint32_t address)
{
    return read_peripheral((DSPState *)core->opaque, address);
}

static void c_write_peripheral(dsp_core_t *core, uint32_t address,
                               uint32_t value)
{
    write_peripheral((DSPState *)core->opaque, address, value);
}

static void dsp_c_reset(DSPState *dsp)
{
    dsp56k_reset_cpu(c_core(dsp));
    dsp->save_cycles = 0;
}

static void dsp_c_step(DSPState *dsp)
{
    dsp56k_execute_instruction(c_core(dsp));
}

/* A halted frame can leave positive credit. Preserve ordinary carry, but
 * never let accumulated host budgets wrap into negative execution debt. */
static void dsp_c_add_cycles(DSPState *dsp, int cycles)
{
    int64_t total = (int64_t)dsp->save_cycles + cycles;
    dsp->save_cycles = total > INT32_MAX ? INT32_MAX :
                       total < INT32_MIN ? INT32_MIN : total;
}

static void dsp_c_run(DSPState *dsp, int cycles)
{
    dsp_core_t *core = c_core(dsp);

    dsp_c_add_cycles(dsp, cycles);

    if (dsp->save_cycles <= 0)
        return;

    if (core->jit) {
        dspjit_run(core->jit, dsp->save_cycles);
        dsp->save_cycles = core->jit_cycles;
    } else
    while (dsp->save_cycles > 0) {
        dsp56k_execute_instruction(core);
        dsp->save_cycles -= core->instr_cycle;
        core->cycle_count += core->instr_cycle;

        if (core->is_idle) {
            break;
        }
    }
}

/* The real-time engine did nothing between consecutive 1000-cycle runs.
 * Let native chains renew that same quantum without leaving host code. */
static void dsp_c_run_until_halt(DSPState *dsp)
{
    dsp_core_t *core = c_core(dsp);
    if (core->jit) {
        dsp_c_add_cycles(dsp, 1000);
        dspjit_run_until_halt(core->jit, dsp->save_cycles);
        dsp->save_cycles = core->jit_cycles;
    } else {
        do {
            dsp_c_run(dsp, 1000);
        } while (!core->is_idle);
    }
}

typedef struct BootstrapLoad {
    uint32_t *base;
    uint32_t *at;
    unsigned partial;
} BootstrapLoad;

static inline void dsp_c_bootstrap_word(BootstrapLoad *load, uint32_t v)
{
    if (v & 0xff000000) {
        DPRINTF("Bootstrap %04x: %08x\n", (unsigned)(load->at - load->base), v);
    }
    *load->at++ = v & 0x00ffffff;
}

/* Copy and normalize each complete word in the borrowed scratch span.
 * A walker may split a word; assemble that word before masking it. */
static void dsp_c_bootstrap_run(void *user, uint8_t *host, size_t bytes)
{
    BootstrapLoad *load = user;
    if (load->partial) {
        size_t n = MIN(bytes, sizeof(uint32_t) - load->partial);
        memcpy((uint8_t *)load->at + load->partial, host, n);
        load->partial += n;
        host += n;
        bytes -= n;
        if (load->partial == sizeof(uint32_t)) {
            dsp_c_bootstrap_word(load, *load->at);
            load->partial = 0;
        }
    }
    while (bytes >= sizeof(uint32_t)) {
        uint32_t v;
        memcpy(&v, host, sizeof(v));
        dsp_c_bootstrap_word(load, v);
        host += sizeof(v);
        bytes -= sizeof(v);
    }
    if (bytes) {
        memcpy(load->at, host, bytes);
        load->partial = bytes;
    }
}

static void dsp_c_bootstrap(DSPState *dsp)
{
    dsp_core_t *core = c_core(dsp);

    /* Scratch memory is DMA'd into PRAM by the boot ROM. Normalize on the
     * way in rather than reading all 0x800 destination words a second time. */
    BootstrapLoad load = { .base = core->pram, .at = core->pram };
    dsp->dma.scratch_walk(dsp->dma.rw_opaque, 0, 0x800 * 4, false,
                          dsp_c_bootstrap_run, &load);
    assert(load.partial == 0);
    dsp56k_clear_opcache(core);
    if (core->jit) {
        dspjit_flush(core->jit);
    }
}

static uint32_t dsp_c_read_memory(DSPState *dsp, char space, uint32_t address)
{
    int space_id;

    switch (space) {
    case 'X':
        space_id = DSP_SPACE_X;
        break;
    case 'Y':
        space_id = DSP_SPACE_Y;
        break;
    case 'P':
        space_id = DSP_SPACE_P;
        break;
    default:
        assert(!"Invalid dsp space when reading from memory");
        return 0;
    }

    return dsp56k_read_memory(c_core(dsp), space_id, address);
}

static void dsp_c_write_memory(DSPState *dsp, char space, uint32_t address,
                               uint32_t value)
{
    int space_id;

    switch (space) {
    case 'X':
        space_id = DSP_SPACE_X;
        break;
    case 'Y':
        space_id = DSP_SPACE_Y;
        break;
    case 'P':
        space_id = DSP_SPACE_P;
        break;
    default:
        assert(!"Invalid dsp space when writing to memory");
        return;
    }

    dsp56k_write_memory(c_core(dsp), space_id, address, value);
}

static uint32_t *dsp_c_memory_ptr(DSPState *dsp, char space, uint32_t addr,
                                  uint32_t count)
{
    int space_id = space == 'X' ? DSP_SPACE_X : space == 'Y' ? DSP_SPACE_Y : DSP_SPACE_P;

    /* the engine's writes to P would need the JIT's code dropped */
    return dsp56k_memory_ptr(c_core(dsp), space_id, addr, count, space_id == DSP_SPACE_P);
}

static bool dsp_c_get_halt_requested(DSPState *dsp)
{
    return c_core(dsp)->is_idle;
}

static void dsp_c_set_halt_requested(DSPState *dsp, bool halt)
{
    c_core(dsp)->is_idle = halt;
}

static uint32_t dsp_c_get_cycle_count(DSPState *dsp)
{
    return c_core(dsp)->cycle_count;
}

static void dsp_c_set_cycle_count(DSPState *dsp, uint32_t count)
{
    c_core(dsp)->cycle_count = count;
}

static void dsp_c_invalidate_opcache(DSPState *dsp)
{
    dsp56k_clear_opcache(c_core(dsp));
    if (c_core(dsp)->jit) {
        dspjit_flush(c_core(dsp)->jit);
    }
}

/*
 * Sync: C interpreter -> DspCoreState (before VM save / debug)
 */
static void dsp_c_sync_to_vm(DSPState *dsp)
{
    dsp_core_t *core = c_core(dsp);
    DspCoreState *vm = &dsp->core;

    vm->pc = core->pc;
    vm->cycle_count = core->cycle_count;
    vm->instr_cycle = core->instr_cycle;
    vm->halt_requested = core->is_idle;
    vm->is_gp = core->is_gp;
    vm->loop_rep = core->loop_rep;
    vm->pc_on_rep = core->pc_on_rep;
    vm->cur_inst = core->cur_inst;
    vm->cur_inst_len = core->cur_inst_len;

    memcpy(vm->registers, core->registers, sizeof(vm->registers));
    memcpy(vm->stack, core->stack, sizeof(vm->stack));
    memcpy(vm->xram, core->xram, sizeof(vm->xram));
    memcpy(vm->yram, core->yram, sizeof(vm->yram));
    memcpy(vm->pram, core->pram, sizeof(vm->pram));
    memcpy(vm->mixbuffer, core->mixbuffer, sizeof(vm->mixbuffer));
    memcpy(vm->periph, core->periph, sizeof(vm->periph));

    vm->interrupt_state = core->interrupt_state;
    vm->interrupt_instr_fetch = core->interrupt_instr_fetch;
    vm->interrupt_save_pc = core->interrupt_save_pc;
    vm->interrupt_counter = core->interrupt_counter;
    vm->interrupt_ipl_to_raise = core->interrupt_ipl_to_raise;
    vm->interrupt_pipeline_count = core->interrupt_pipeline_count;
    memcpy(vm->interrupt_ipl, core->interrupt_ipl, sizeof(core->interrupt_ipl));
    memcpy(vm->interrupt_is_pending, core->interrupt_is_pending,
           sizeof(core->interrupt_is_pending));
}

/*
 * Sync: DspCoreState -> C interpreter (after VM load / engine switch)
 */
static void dsp_c_sync_from_vm(DSPState *dsp)
{
    dsp_core_t *core = c_core(dsp);
    DspCoreState *vm = &dsp->core;

    core->pc = vm->pc;
    core->cycle_count = vm->cycle_count;
    core->instr_cycle = vm->instr_cycle;
    core->is_idle = vm->halt_requested;
    core->is_gp = vm->is_gp;
    core->loop_rep = vm->loop_rep;
    core->pc_on_rep = vm->pc_on_rep;
    core->cur_inst = vm->cur_inst;
    core->cur_inst_len = vm->cur_inst_len;

    memcpy(core->registers, vm->registers, sizeof(vm->registers));
    memcpy(core->stack, vm->stack, sizeof(vm->stack));
    memcpy(core->xram, vm->xram, sizeof(vm->xram));
    memcpy(core->yram, vm->yram, sizeof(vm->yram));
    memcpy(core->pram, vm->pram, sizeof(vm->pram));
    memcpy(core->mixbuffer, vm->mixbuffer, sizeof(vm->mixbuffer));
    memcpy(core->periph, vm->periph, sizeof(vm->periph));

    core->interrupt_state = vm->interrupt_state;
    core->interrupt_instr_fetch = vm->interrupt_instr_fetch;
    core->interrupt_save_pc = vm->interrupt_save_pc;
    core->interrupt_counter = vm->interrupt_counter;
    core->interrupt_ipl_to_raise = vm->interrupt_ipl_to_raise;
    core->interrupt_pipeline_count = vm->interrupt_pipeline_count;
    memcpy(core->interrupt_ipl, vm->interrupt_ipl, sizeof(core->interrupt_ipl));
    memcpy(core->interrupt_is_pending, vm->interrupt_is_pending,
           sizeof(core->interrupt_is_pending));

    dsp56k_clear_opcache(core);
}

void dsp_c_init(DSPState *dsp)
{
    dsp_core_t *core = g_new(dsp_core_t, 1);
    /* RAM gets its power-on pattern directly, rather than zeroing it first.
     * These three arrays must remain adjacent for this initialization span. */
    _Static_assert(offsetof(dsp_core_t, yram) == offsetof(dsp_core_t, xram) + sizeof(core->xram), "X/Y RAM layout");
    _Static_assert(offsetof(dsp_core_t, pram) == offsetof(dsp_core_t, yram) + sizeof(core->yram), "Y/P RAM layout");
    const size_t ram_begin = offsetof(dsp_core_t, xram);
    const size_t ram_end = offsetof(dsp_core_t, pram) + sizeof(core->pram);
    memset(core, 0, ram_begin);
    memset((uint8_t *)core + ram_begin, 0xCA, ram_end - ram_begin);
    memset((uint8_t *)core + ram_end, 0, sizeof(*core) - ram_end);
    core->opaque = dsp;
    core->read_peripheral = c_read_peripheral;
    core->write_peripheral = c_write_peripheral;

    dsp->backend = core;
    dsp->ops = &c_dsp_ops;

    dsp->dma.mem_opaque = core;
    dsp->dma.mem_read = c_dma_mem_read;
    dsp->dma.mem_write = c_dma_mem_write;
    dsp->dma.mem_ptr = c_dma_mem_ptr;
    dsp->dma.mem_descriptor = c_dma_mem_descriptor;

    /* Ensure the interpreter's opcode decoder tables are initialized.
     * dsp56k_reset_cpu populates the static nonparallel_matches[] array
     * on first call.  The runtime state it writes (registers, PC, etc.)
     * will be overwritten by the subsequent sync_from_vm. */
    dsp56k_reset_cpu(core);

    dsp->ops->invalidate_opcache(dsp);
}

static void dsp_c_finalize(DSPState *dsp)
{
    dspjit_destroy(c_core(dsp)->jit);
    g_free(dsp->backend);
    dsp->backend = NULL;
}

/* The interpreter's state run by the JIT (jit/dspjit.c); the interpreter
 * where the host has no JIT */
void dsp_jit_init(DSPState *dsp)
{
    dsp_c_init(dsp);
    c_core(dsp)->jit = dspjit_create(c_core(dsp));
    if (c_core(dsp)->jit) {
        dsp->ops = &jit_dsp_ops;
    }
}

const DSPOps jit_dsp_ops = {
    .bootstrap = dsp_c_bootstrap,
    .finalize = dsp_c_finalize,
    .get_cycle_count = dsp_c_get_cycle_count,
    .get_halt_requested = dsp_c_get_halt_requested,
    .invalidate_opcache = dsp_c_invalidate_opcache,
    .read_memory = dsp_c_read_memory,
    .reset = dsp_c_reset,
    .run = dsp_c_run,
    .run_until_halt = dsp_c_run_until_halt,
    .set_cycle_count = dsp_c_set_cycle_count,
    .set_halt_requested = dsp_c_set_halt_requested,
    .start_frame = dsp_start_frame_impl,
    .step = dsp_c_step,
    .sync_from_vm = dsp_c_sync_from_vm,
    .sync_to_vm = dsp_c_sync_to_vm,
    .write_memory = dsp_c_write_memory,
    .memory_ptr = dsp_c_memory_ptr,
};

const DSPOps c_dsp_ops = {
    .bootstrap = dsp_c_bootstrap,
    .finalize = dsp_c_finalize,
    .get_cycle_count = dsp_c_get_cycle_count,
    .get_halt_requested = dsp_c_get_halt_requested,
    .invalidate_opcache = dsp_c_invalidate_opcache,
    .read_memory = dsp_c_read_memory,
    .reset = dsp_c_reset,
    .run = dsp_c_run,
    .run_until_halt = dsp_c_run_until_halt,
    .set_cycle_count = dsp_c_set_cycle_count,
    .set_halt_requested = dsp_c_set_halt_requested,
    .start_frame = dsp_start_frame_impl,
    .step = dsp_c_step,
    .sync_from_vm = dsp_c_sync_from_vm,
    .sync_to_vm = dsp_c_sync_to_vm,
    .write_memory = dsp_c_write_memory,
    .memory_ptr = dsp_c_memory_ptr,
};

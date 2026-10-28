/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2018-2019 Jannik Vogel
 * Copyright (c) 2019-2025 Matt Borgerson
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

#include "apu_int.h"

MCPXAPUState *g_state; // Used via debug handlers

static void update_irq(MCPXAPUState *d)
{
    if (d->regs[NV_PAPU_FECTL] & NV_PAPU_FECTL_FEMETHMODE_TRAPPED) {
        qatomic_or(&d->regs[NV_PAPU_ISTS], NV_PAPU_ISTS_FETINTSTS);
    }
    if ((d->regs[NV_PAPU_IEN] & NV_PAPU_ISTS_GINTSTS) &&
        ((d->regs[NV_PAPU_ISTS] & ~NV_PAPU_ISTS_GINTSTS) &
         d->regs[NV_PAPU_IEN])) {
        qatomic_or(&d->regs[NV_PAPU_ISTS], NV_PAPU_ISTS_GINTSTS);
        // fprintf(stderr, "mcpx irq raise ien=%08x ists=%08x\n",
        //         d->regs[NV_PAPU_IEN], d->regs[NV_PAPU_ISTS]);
        pci_irq_assert(PCI_DEVICE(d));
    } else {
        qatomic_and(&d->regs[NV_PAPU_ISTS], ~NV_PAPU_ISTS_GINTSTS);
        // fprintf(stderr, "mcpx irq lower ien=%08x ists=%08x\n",
        //         d->regs[NV_PAPU_IEN], d->regs[NV_PAPU_ISTS]);
        pci_irq_deassert(PCI_DEVICE(d));
    }
}

static uint64_t mcpx_apu_read(void *opaque, hwaddr addr, unsigned int size)
{
    MCPXAPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PAPU_XGSCNT:
        r = (uint64_t)d->ep_frame_div * NUM_SAMPLES_PER_FRAME;
        break;
    default:
        if (addr < 0x20000) {
            r = qatomic_read(&d->regs[addr]);
        }
        break;
    }

    trace_mcpx_apu_reg_read(addr, size, r);
    return r;
}

static void mcpx_apu_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned int size)
{
    MCPXAPUState *d = opaque;

    trace_mcpx_apu_reg_write(addr, size, val);

    switch (addr) {
    case NV_PAPU_ISTS:
        /* the bits of the interrupts to clear are written */
        qatomic_and(&d->regs[NV_PAPU_ISTS], ~val);
        update_irq(d);
        break;
    case NV_PAPU_FECTL:
    case NV_PAPU_SECTL:
        qatomic_set(&d->regs[addr], val);
        break;
    case NV_PAPU_FEMEMDATA:
        /* 'magic write'
         * This value is expected to be written to FEMEMADDR on completion of
         * something to do with notifies. Just do it now :/ */
        stl_le_phys(&address_space_memory, d->regs[NV_PAPU_FEMEMADDR], val);
        qatomic_set(&d->regs[addr], val);
        break;
    default:
        if (addr < 0x20000) {
            qatomic_set(&d->regs[addr], val);
        }
        break;
    }
}

static const MemoryRegionOps mcpx_apu_mmio_ops = {
    .read = mcpx_apu_read,
    .write = mcpx_apu_write,
};

static void se_frame(MCPXAPUState *d)
{
    mcpx_apu_update_dsp_preference(d);
    mcpx_debug_begin_frame();
    g_dbg.gp_realtime = d->gp.realtime;
    g_dbg.ep_realtime = d->ep.realtime;

    int64_t start_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    int64_t elapsed_us = start_us - d->frame_count_time_us;
    if (elapsed_us >= 1000000) {
        /* A rudimentary calculation to determine approximately how taxed the
         * emulator thread is by the APU, from the time spent building frames */
        g_dbg.utilization = (double)d->frame_work_acc_us / (double)elapsed_us;
        g_dbg.frames_processed = (int)(d->frame_count * 1000000.0 / elapsed_us + 0.5);

        d->frame_count_time_us = start_us;
        d->frame_count = 0;
        d->frame_work_acc_us = 0;
    }
    d->frame_count++;

    /* Buffer for all mixbins for this frame */
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = { 0 };

    mcpx_apu_vp_frame(d, mixbins);
    mcpx_apu_dsp_frame(d, mixbins);
    mcpx_apu_monitor_frame(d);

    d->ep_frame_div++;
    d->frame_work_acc_us += qemu_clock_get_us(QEMU_CLOCK_REALTIME) - start_us;

    mcpx_debug_end_frame();
}

/*
 * The sound engine's frames, one every 32 samples at 48 kHz, on
 * QEMU_CLOCK_VIRTUAL: they run on the emulator thread as the guest's time
 * passes, so each video frame of guest time gets exactly its samples
 * whatever the host's speed (ui/libretro.c hands them to the frontend). A
 * frame with the engine stopped is silence. A frame that falls while the
 * guest holds a voice locked waits until it lets go: the guest is in the
 * middle of changing that voice.
 */
#define APU_TICK_NS_NUM 2000000 /* 32 samples = 2000000/3 ns */
#define APU_TICK_NS_DEN 3
#define APU_LOCK_RETRY_NS 20000

static bool voices_locked(MCPXAPUState *d)
{
    return d->vp.voice_locked[0] | d->vp.voice_locked[1] |
           d->vp.voice_locked[2] | d->vp.voice_locked[3];
}

static void apu_tick_advance(MCPXAPUState *d)
{
    d->tick_rem += APU_TICK_NS_NUM % APU_TICK_NS_DEN;
    d->next_tick_ns += APU_TICK_NS_NUM / APU_TICK_NS_DEN +
                       d->tick_rem / APU_TICK_NS_DEN;
    d->tick_rem %= APU_TICK_NS_DEN;
}

static void apu_step(MCPXAPUState *d)
{
    int xcntmode = GET_MASK(qatomic_read(&d->regs[NV_PAPU_SECTL]),
                            NV_PAPU_SECTL_XCNTMODE);
    uint32_t fectl = qatomic_read(&d->regs[NV_PAPU_FECTL]);

    if (xcntmode == NV_PAPU_SECTL_XCNTMODE_OFF ||
        (fectl & NV_PAPU_FECTL_FEMETHMODE_TRAPPED) ||
        (fectl & NV_PAPU_FECTL_FEMETHMODE_HALTED)) {
        d->set_irq = true;
    }
    if (d->set_irq) {
        update_irq(d);
        d->set_irq = false;
    }

    xcntmode = GET_MASK(qatomic_read(&d->regs[NV_PAPU_SECTL]),
                        NV_PAPU_SECTL_XCNTMODE);
    fectl = qatomic_read(&d->regs[NV_PAPU_FECTL]);
    if (xcntmode == NV_PAPU_SECTL_XCNTMODE_OFF ||
        (fectl & NV_PAPU_FECTL_FEMETHMODE_TRAPPED) ||
        (fectl & NV_PAPU_FECTL_FEMETHMODE_HALTED)) {
        mcpx_apu_monitor_silence(d);
        return;
    }

    se_frame(d);
    if (d->set_irq) {
        update_irq(d);
        d->set_irq = false;
    }
}

static void apu_tick_cb(void *opaque)
{
    MCPXAPUState *d = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    while (d->next_tick_ns <= now) {
        if (voices_locked(d)) {
            timer_mod_ns(d->tick_timer, now + APU_LOCK_RETRY_NS);
            return;
        }
        apu_step(d);
        apu_tick_advance(d);
    }
    timer_mod_ns(d->tick_timer, d->next_tick_ns);
}

/* The engine's frames start again from now: at start, and after a state
 * was loaded (the clock is the state's) */
static void apu_tick_rearm(MCPXAPUState *d)
{
    d->next_tick_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    d->tick_rem = 0;
    apu_tick_advance(d);
    timer_mod_ns(d->tick_timer, d->next_tick_ns);
}

static void mcpx_apu_reset_locked(MCPXAPUState *d)
{
    memset(d->regs, 0, sizeof(d->regs));

    mcpx_apu_vp_reset(d);

    // FIXME: Reset DSP state
    dsp_invalidate_opcache(d->gp.dsp);
    dsp_invalidate_opcache(d->ep.dsp);
    d->set_irq = false;
}

static void mcpx_apu_reset_hold(Object *obj, ResetType type)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(obj);

    mcpx_apu_reset_locked(d);
}

/* The DSPs' state goes to and comes from the VM's when it stops and starts,
 * as for a savestate; the engine's frames stop with QEMU_CLOCK_VIRTUAL */
static void mcpx_apu_vm_state_change(void *opaque, bool running, RunState state)
{
    MCPXAPUState *d = opaque;

    if (!running) {
        if (d->gp.dsp) {
            dsp_sync_to_vm(d->gp.dsp);
        }
        if (d->ep.dsp) {
            dsp_sync_to_vm(d->ep.dsp);
        }
    } else {
        if (d->gp.dsp) {
            dsp_sync_from_vm(d->gp.dsp);
        }
        if (d->ep.dsp) {
            dsp_sync_from_vm(d->ep.dsp);
        }
    }
}

static int mcpx_apu_pre_load(void *opaque)
{
    MCPXAPUState *d = opaque;
    mcpx_apu_reset_locked(d);
    return 0;
}

static int mcpx_apu_post_load(void *opaque, int version_id)
{
    MCPXAPUState *d = opaque;
    apu_tick_rearm(d);
    return 0;
}

static void mcpx_apu_realize(PCIDevice *dev, Error **errp)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(dev);

    dev->config[PCI_INTERRUPT_PIN] = 0x01;

    memory_region_init_io(&d->mmio, OBJECT(dev), &mcpx_apu_mmio_ops, d,
                          "mcpx-apu-mmio", 0x80000);

    memory_region_init_io(&d->vp.mmio, OBJECT(dev), &vp_ops, d,
                          "mcpx-apu-vp", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x20000, &d->vp.mmio);

    memory_region_init_io(&d->gp.mmio, OBJECT(dev), &gp_ops, d,
                          "mcpx-apu-gp", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x30000, &d->gp.mmio);

    memory_region_init_io(&d->ep.mmio, OBJECT(dev), &ep_ops, d,
                          "mcpx-apu-ep", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x50000, &d->ep.mmio);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    d->set_irq = false;

    mcpx_apu_vp_init(d);
    mcpx_apu_dsp_init(d);

    Error *local_err = NULL;
    mcpx_apu_monitor_init(d, &local_err);
    if (local_err) {
        warn_reportf_err(local_err, "mcpx_apu_monitor_init failed: ");
    }

    qemu_add_vm_change_state_handler(mcpx_apu_vm_state_change, d);
    d->tick_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, apu_tick_cb, d);
    apu_tick_rearm(d);
}

static void mcpx_apu_exitfn(PCIDevice *dev)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(dev);

    timer_free(d->tick_timer);
    d->tick_timer = NULL;
    mcpx_apu_vp_finalize(d);
    mcpx_apu_monitor_finalize(d);
}

static bool vp_dsp_dma_read_count_needed(void *opaque)
{
    DSPDMAState *s = opaque;
    return s->dma_read_count != 0;
}

static const VMStateDescription vmstate_vp_dsp_dma_read_count = {
    .name = "mcpx-apu/dsp-state/dma/dma_read_count",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vp_dsp_dma_read_count_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(dma_read_count, DSPDMAState),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_vp_dsp_dma_state = {
    .name = "mcpx-apu/dsp-state/dma",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(configuration, DSPDMAState),
        VMSTATE_UINT32(control, DSPDMAState),
        VMSTATE_UINT32(start_block, DSPDMAState),
        VMSTATE_UINT32(next_block, DSPDMAState),
        VMSTATE_BOOL(error, DSPDMAState),
        VMSTATE_BOOL(eol, DSPDMAState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_vp_dsp_dma_read_count,
        NULL
    }
};

const VMStateDescription vmstate_vp_dsp_core_state = {
    .name = "mcpx-apu/dsp-state/core",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT16(instr_cycle, DspCoreState),
        VMSTATE_UINT32(pc, DspCoreState),
        VMSTATE_UINT32_ARRAY(registers, DspCoreState, DSP_REG_MAX),
        VMSTATE_UINT32_2DARRAY(stack, DspCoreState, 2, 16),
        VMSTATE_UINT32_ARRAY(xram, DspCoreState, DSP_XRAM_SIZE),
        VMSTATE_UINT32_ARRAY(yram, DspCoreState, DSP_YRAM_SIZE),
        VMSTATE_UINT32_ARRAY(pram, DspCoreState, DSP_PRAM_SIZE),
        VMSTATE_UINT32_ARRAY(mixbuffer, DspCoreState, DSP_MIXBUFFER_SIZE),
        VMSTATE_UINT32_ARRAY(periph, DspCoreState, DSP_PERIPH_SIZE),
        VMSTATE_UINT32(loop_rep, DspCoreState),
        VMSTATE_UINT32(pc_on_rep, DspCoreState),
        VMSTATE_UINT16(interrupt_state, DspCoreState),
        VMSTATE_UINT16(interrupt_instr_fetch, DspCoreState),
        VMSTATE_UINT16(interrupt_save_pc, DspCoreState),
        VMSTATE_UINT16(interrupt_counter, DspCoreState),
        VMSTATE_UINT16(interrupt_ipl_to_raise, DspCoreState),
        VMSTATE_UINT16(interrupt_pipeline_count, DspCoreState),
        VMSTATE_INT16_ARRAY(interrupt_ipl, DspCoreState, 12),
        VMSTATE_UINT16_ARRAY(interrupt_is_pending, DspCoreState, 12),
        VMSTATE_UNUSED(4),   /* was num_inst */
        VMSTATE_UINT32(cur_inst_len, DspCoreState),
        VMSTATE_UINT32(cur_inst, DspCoreState),
        VMSTATE_UNUSED(273), /* was: unused(1) + disasm_memory_ptr(4) +
                              * exception_debugging(1) + disasm_prev_inst_pc(4) +
                              * disasm_is_looping(1) + disasm_cur_inst(4) +
                              * disasm_cur_inst_len(2) +
                              * disasm_registers_save(256) */
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_vp_dsp_state = {
    .name = "mcpx-apu/dsp-state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(core, DSPState, 1, vmstate_vp_dsp_core_state, DspCoreState),
        VMSTATE_STRUCT(dma, DSPState, 1, vmstate_vp_dsp_dma_state, DSPDMAState),
        VMSTATE_INT32(save_cycles, DSPState),
        VMSTATE_UINT32(interrupts, DSPState),
        VMSTATE_END_OF_LIST()
    }
};


const VMStateDescription vmstate_vp_ssl_data = {
    .name = "mcpx_apu_voice_data",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(base, MCPXAPUVPSSLData, MCPX_HW_SSLS_PER_VOICE),
        VMSTATE_UINT8_ARRAY(count, MCPXAPUVPSSLData, MCPX_HW_SSLS_PER_VOICE),
        VMSTATE_INT32(ssl_index, MCPXAPUVPSSLData),
        VMSTATE_INT32(ssl_seg, MCPXAPUVPSSLData),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_mcpx_apu = {
    .name = "mcpx-apu",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = mcpx_apu_pre_load,
    .post_load = mcpx_apu_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, MCPXAPUState),
        VMSTATE_STRUCT_POINTER(gp.dsp, MCPXAPUState, vmstate_vp_dsp_state,
                               DSPState),
        VMSTATE_UINT32_ARRAY(gp.regs, MCPXAPUState, 0x10000),
        VMSTATE_STRUCT_POINTER(ep.dsp, MCPXAPUState, vmstate_vp_dsp_state,
                               DSPState),
        VMSTATE_UINT32_ARRAY(ep.regs, MCPXAPUState, 0x10000),
        VMSTATE_UINT32_ARRAY(regs, MCPXAPUState, 0x20000),
        VMSTATE_UINT32(vp.inbuf_sge_handle, MCPXAPUState),
        VMSTATE_UINT32(vp.outbuf_sge_handle, MCPXAPUState),
        VMSTATE_STRUCT_ARRAY(vp.ssl, MCPXAPUState, MCPX_HW_MAX_VOICES, 1,
                             vmstate_vp_ssl_data, MCPXAPUVPSSLData),
        VMSTATE_INT32(vp.ssl_base_page, MCPXAPUState),
        VMSTATE_UINT8_ARRAY(vp.hrtf_submix, MCPXAPUState, 4),
        VMSTATE_UINT8(vp.hrtf_headroom, MCPXAPUState),
        VMSTATE_UINT8_ARRAY(vp.submix_headroom, MCPXAPUState, NUM_MIXBINS),
        VMSTATE_UINT64_ARRAY(vp.voice_locked, MCPXAPUState, 4),
        VMSTATE_END_OF_LIST()
    },
};

static void mcpx_apu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_MCPX_APU;
    k->revision = 177;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    k->realize = mcpx_apu_realize;
    k->exit = mcpx_apu_exitfn;

    rc->phases.hold = mcpx_apu_reset_hold;

    dc->desc = "MCPX Audio Processing Unit";
    dc->vmsd = &vmstate_mcpx_apu;
}

static const TypeInfo mcpx_apu_info = {
    .name = "mcpx-apu",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPXAPUState),
    .class_init = mcpx_apu_class_init,
    .interfaces =
        (InterfaceInfo[]){
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            {},
        },
};

static void mcpx_apu_register(void)
{
    type_register_static(&mcpx_apu_info);
}
type_init(mcpx_apu_register);

void mcpx_apu_init(PCIBus *bus, int devfn, MemoryRegion *ram)
{
    PCIDevice *dev = pci_create_simple(bus, devfn, "mcpx-apu");
    MCPXAPUState *d = MCPX_APU_DEVICE(dev);

    g_state = d;

    d->ram = ram;
    d->ram_ptr = memory_region_get_ram_ptr(d->ram);
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#define MiB (1024u*1024u)
#define VGA_MIS_COLOR 1
#define PCI_DEVICE(d) (d)
#define retro_atomic_load_acquire_int(p) (*(p))
#define retro_atomic_load_relaxed_int(p) (*(p))
#define retro_atomic_store_relaxed_int(p,v) (*(p)=(v))
#define retro_atomic_store_release_int(p,v) (*(p)=(v))
typedef struct { uint32_t pending_interrupts,enabled_interrupts; } Intr;
typedef struct {
    Intr pmc,ptimer;
    struct { uint32_t pending_interrupts,enabled_interrupts,regs[16]; int thread_running,halt; } pfifo;
    struct { int pending_interrupts,enabled_interrupts,flush_pending,flush_complete;
             int waiting_for_flip,guest_wrote_regs; uint32_t regs_[16];
             float back_material_alpha,specular_power_back,specular_params_back[6]; } pgraph;
    struct { uint32_t regs[16]; } pvideo;
    struct { uint32_t pending_interrupts,enabled_interrupts,start; } pcrtc;
    struct { uint32_t core_clock_coeff,core_clock_freq,memory_clock_coeff,video_clock_coeff; } pramdac;
    struct { uint8_t palette[768]; int packed[256],dirty; } puserdac;
    struct { int msr; } vga;
    uint64_t *vram;
    bool unified;
} NV2AState;
static bool irq, fifo_locked, bql_held;
static unsigned flushes,updates;
static void trace_nv2a_irq(uint32_t bits) { assert(bits); }
static void pci_irq_assert(NV2AState *d) { (void)d;irq=true;updates++; }
static void pci_irq_deassert(NV2AState *d) { (void)d;irq=false;updates++; }
static void nv2a_migration_release(NV2AState *d) { (void)d;assert(bql_held&&!fifo_locked); }
static void nv2a_lock_fifo(NV2AState *d) { (void)d;assert(!fifo_locked&&bql_held);fifo_locked=true; }
static void nv2a_unlock_fifo(NV2AState *d) { (void)d;assert(fifo_locked);fifo_locked=false; }
static void bql_unlock(void) { assert(bql_held&&!fifo_locked);bql_held=false; }
static void bql_lock(void) { assert(!bql_held);bql_held=true; }
static void pgraph_wait_until_clear(int *complete,int *pending)
{ (void)complete;assert(!bql_held&&!fifo_locked&&*pending);*pending=0;flushes++; }
static uint64_t memory_region_size(uint64_t *size) { return *size; }
static void nv2a_pramin_set_mode(NV2AState *d,bool unified) { assert(bql_held);d->unified=unified; }
static uint32_t pfifo_reg_r(NV2AState *d,unsigned addr) { (void)addr;return d->pfifo.regs[0]; }
static void pfifo_reg_w(NV2AState *d,unsigned addr,uint32_t value) { (void)addr;d->pfifo.regs[0]=value; }
static void vga_common_reset(void *vga) { (void)vga;assert(bql_held); }
static void ptimer_reset(NV2AState *d) { (void)d;assert(bql_held); }
/* PRODUCTION */
int main(void)
{
    unsigned cases=0;
    for(unsigned bits=0;bits<1024;bits++)
    for(unsigned renderer=0;renderer<2;renderer++)
    for(unsigned halted=0;halted<2;halted++)
    for(unsigned wide=0;wide<2;wide++) {
        NV2AState d;memset(&d,0xa5,sizeof(d));
        uint64_t size=(wide?128:64)*MiB;d.vram=&size;
        d.pfifo.thread_running=renderer;d.pfifo.halt=halted;
        d.pmc.enabled_interrupts=(bits>>0)&1;d.pmc.pending_interrupts=0;
        d.pfifo.enabled_interrupts=(bits>>1)&1;d.pfifo.pending_interrupts=(bits>>5)&1;
        d.pgraph.enabled_interrupts=(bits>>2)&1;d.pgraph.pending_interrupts=(bits>>6)&1;
        d.ptimer.enabled_interrupts=(bits>>3)&1;d.ptimer.pending_interrupts=(bits>>7)&1;
        d.pcrtc.enabled_interrupts=(bits>>4)&1;d.pcrtc.pending_interrupts=(bits>>8)&1;
        bql_held=true;fifo_locked=false;irq=(bits>>9)&1;updates=flushes=0;
        nv2a_reset(&d);
        assert(!irq && updates && !fifo_locked && bql_held);
        assert(flushes==renderer && d.pfifo.halt==(int)halted && d.unified==!wide);
        assert(!d.pmc.pending_interrupts && !d.pmc.enabled_interrupts);
        assert(!d.pfifo.pending_interrupts && !d.pfifo.enabled_interrupts);
        assert(!d.pgraph.pending_interrupts && !d.pgraph.enabled_interrupts);
        assert(!d.ptimer.pending_interrupts && !d.ptimer.enabled_interrupts);
        assert(!d.pcrtc.pending_interrupts && !d.pcrtc.enabled_interrupts);
        assert(!d.pgraph.waiting_for_flip && !d.pgraph.guest_wrote_regs);
        for(unsigned i=0;i<256;i++) {
            assert(d.puserdac.packed[i]==(int)(i|(i<<8)|(i<<16)));
            for(unsigned k=0;k<3;k++)assert(d.puserdac.palette[i*3+k]==i);
        }
        /* New device activity must stay masked until the guest enables it. */
        d.pfifo.pending_interrupts=d.pgraph.pending_interrupts=1;
        d.ptimer.pending_interrupts=d.pcrtc.pending_interrupts=1;
        nv2a_update_irq(&d);assert(!irq);
        d.pmc.enabled_interrupts=1;nv2a_update_irq(&d);assert(!irq);
        for(unsigned unit=0;unit<4;unit++) {
            uint32_t *enable=unit==0?&d.pfifo.enabled_interrupts:unit==2?&d.ptimer.enabled_interrupts:&d.pcrtc.enabled_interrupts;
            if(unit==1)d.pgraph.enabled_interrupts=1;else *enable=1;
            nv2a_update_irq(&d);assert(irq);
            if(unit==1)d.pgraph.enabled_interrupts=0;else *enable=0;
            nv2a_update_irq(&d);assert(!irq);
        }
        nv2a_reset(&d);assert(!irq&&!d.pmc.pending_interrupts&&!d.pmc.enabled_interrupts);
        cases+=2;
    }
    printf("PASS: %u NV2A resets; initial IRQs, every pending/enable combination, renderer lock paths, late events and re-enable\n",cases);
}

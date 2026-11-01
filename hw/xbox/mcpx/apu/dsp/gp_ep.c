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

#include "hw/xbox/mcpx/apu/apu_int.h"
#include "hw/xbox/mcpx/apu/monitor.h"
#include "exec/cputlb.h"
#include "libretro/dsp-ownership.h"
#include "bus-lease.h"
#include "qemu/rcu.h"
#include "qemu/processor.h"
#include "system/physmem.h"

static DSPBusLease bus_lease;
static retro_eventcount_t gpu_ownership_changed;
static bool bus_lease_ready;
void lr_dsp_bus_init(void)
{
    /* retro_init is serialized by the frontend. Keep the gate for the
     * pinned module's lifetime; repeated content loads must not reset it. */
    if (!bus_lease_ready) {
        if (!bus_lease_init(&bus_lease) || !retro_eventcount_init(&gpu_ownership_changed)) abort();
        bus_lease_ready = true;
    }
}
void lr_dsp_bus_borrow(void) { bus_lease_acquire(&bus_lease); }
void lr_dsp_bus_return(void) { bus_lease_release(&bus_lease); }
/* The CPU owns GP. One EP job may own guest RAM until its completion
 * epoch is published. No job queue, mix-buffer copy or per-tick allocation. */
static QemuThread ownership_thread;
static retro_atomic_int_t ownership_posted, ownership_done, ownership_stop;
static retro_eventcount_t ownership_work, ownership_completion;
static retro_atomic_int_t ownership_ready;
static bool ownership_enabled;
static MCPXAPUState *ownership_device;
static int ownership_div;
static const uint64_t *ownership_readonly_pages;
static __thread bool ownership_is_worker;
static __thread bool ownership_is_vp_worker;
static __thread bool ownership_gp_dma_guard;
static __thread unsigned ownership_cpu_depth;
void lr_dsp_vp_worker(bool active) { ownership_is_vp_worker = active; }

static void dsp_run_ep(MCPXAPUState *d, int frame_div);
static void gpu_release_dsp_pages(void);
static void ownership_relax_if_cpu(void);
void lr_dsp_ownership_quiesce(void)
{
    if (!retro_atomic_load_acquire_int(&ownership_ready) || ownership_is_worker) return;
    int epoch = retro_atomic_load_acquire_int(&ownership_posted);
    while (retro_atomic_load_acquire_int(&ownership_done) != epoch) {
        int key = retro_eventcount_prepare_wait(&ownership_completion);
        if (retro_atomic_load_acquire_int(&ownership_done) == epoch)
            retro_eventcount_cancel_wait(&ownership_completion);
        else retro_eventcount_commit_wait(&ownership_completion, key);
    }
    ownership_relax_if_cpu();
}
void lr_dsp_ownership_fence(unsigned reason)
{
    (void)reason;
    lr_dsp_ownership_quiesce();
}
static void *ownership_worker(void *opaque)
{
    MCPXAPUState *d = opaque;
    ownership_is_worker = true;
    rcu_register_thread();
    for (;;) {
        int epoch = retro_atomic_load_acquire_int(&ownership_posted);
        if (epoch == retro_atomic_load_relaxed_int(&ownership_done)) {
            if (retro_atomic_load_acquire_int(&ownership_stop)) break;
            int key = retro_eventcount_prepare_wait(&ownership_work);
            if (retro_atomic_load_acquire_int(&ownership_posted) != epoch ||
                retro_atomic_load_acquire_int(&ownership_stop))
                retro_eventcount_cancel_wait(&ownership_work);
            else retro_eventcount_commit_wait(&ownership_work, key);
            continue;
        }
        rcu_read_lock();
        dsp_run_ep(d, ownership_div);
        rcu_read_unlock();
        gpu_release_dsp_pages();
        bus_lease_dsp_done(&bus_lease);
        retro_atomic_store_release_int(&ownership_done, epoch);
        retro_eventcount_notify(&ownership_completion);
    }
    rcu_unregister_thread();
    return NULL;
}
static void ownership_start(MCPXAPUState *d)
{
    if (retro_atomic_load_acquire_int(&ownership_ready)) return;
#if defined(RETRO_ATOMIC_LOCK_FREE)
    ownership_enabled = true;
#else
    /* Unsupported atomic targets retain synchronous execution. */
    ownership_enabled = false;
#endif
    if (!ownership_enabled) return;
    retro_atomic_int_init(&ownership_posted, 0);
    retro_atomic_int_init(&ownership_done, 0);
    retro_atomic_int_init(&ownership_stop, 0);
    if (!retro_eventcount_init(&ownership_work)) {
        ownership_enabled = false;
        return;
    }
    if (!retro_eventcount_init(&ownership_completion)) {
        retro_eventcount_free(&ownership_work);
        ownership_enabled = false;
        return;
    }
    ownership_device = d;
    retro_atomic_store_release_int(&ownership_ready, 1);
    qemu_thread_create(&ownership_thread, "apu-ep", ownership_worker, d,
                       QEMU_THREAD_JOINABLE);
}
static void ownership_finish(void)
{
    if (!retro_atomic_load_acquire_int(&ownership_ready)) return;
    lr_dsp_ownership_quiesce();
    retro_atomic_store_release_int(&ownership_stop, 1);
    retro_eventcount_notify(&ownership_work);
    qemu_thread_join(&ownership_thread);
    assert(retro_atomic_load_acquire_int(&ownership_posted) ==
           retro_atomic_load_acquire_int(&ownership_done));
    retro_atomic_store_release_int(&ownership_ready, 0);
    ownership_device = NULL;
    retro_eventcount_free(&ownership_work);
    retro_eventcount_free(&ownership_completion);
}
void lr_dsp_ownership_stop(void) { ownership_finish(); }
void lr_dsp_ownership_resume(void)
{
    if (g_state && g_state->gp.dsp) ownership_start(g_state);
}

static int ownership_page_live(uint64_t page);
int lr_dsp_external_begin(void)
{
    if(ownership_is_worker || ownership_is_vp_worker) return 0;
    /* A live CPU scope already proved this thread owns the CPU. Nested
     * memory/device callbacks only extend that scope until they return. */
    if(ownership_cpu_depth) {ownership_cpu_depth++;return 2;}
    CPUState *cpu=qemu_get_cpu(0);
    if(cpu && qemu_cpu_is_self(cpu)) {ownership_cpu_depth++;return 2;}
    lr_dsp_bus_borrow();return 1;
}
void lr_dsp_cpu_transaction_end(void) {assert(ownership_cpu_depth);ownership_cpu_depth--;}
/* Registered during device initialization on the timer owner. Unknown
 * callbacks retain the full fence. No guest-memory contents are cached. */
static void (*ownership_guarded_timers[16])(void *);
static unsigned ownership_guarded_timer_count;
void lr_dsp_register_guarded_timer(void (*cb)(void *))
{
    for(unsigned i=0;i<ownership_guarded_timer_count;i++)if(ownership_guarded_timers[i]==cb)return;
    if(ownership_guarded_timer_count<16)ownership_guarded_timers[ownership_guarded_timer_count++]=cb;
}
bool lr_dsp_timer_guarded(void (*cb)(void *))
{
    if(lr_dsp_is_tick_callback(cb))return true;
    for(unsigned i=0;i<ownership_guarded_timer_count;i++)if(ownership_guarded_timers[i]==cb)return true;
    return false;
}
/* Only audited register-only regions opt in. Unknown MMIO still fences. */
static const struct MemoryRegionOps *ownership_guarded_mmio[8];
static unsigned ownership_guarded_mmio_count;
void lr_dsp_register_guarded_mmio_ops(const struct MemoryRegionOps *ops)
{
    for(unsigned i=0;i<ownership_guarded_mmio_count;i++)if(ownership_guarded_mmio[i]==ops)return;
    if(ownership_guarded_mmio_count<8)ownership_guarded_mmio[ownership_guarded_mmio_count++]=ops;
}
bool lr_dsp_mmio_guarded(MemoryRegion *mr)
{
    for(unsigned i=0;i<ownership_guarded_mmio_count;i++)if(ownership_guarded_mmio[i]==mr->ops)return true;
    return false;
}
int lr_dsp_timer_begin(void (*cb)(void *))
{return lr_dsp_is_tick_callback(cb)?0:lr_dsp_external_begin();}


static void ownership_ram_access(uint64_t addr,uint64_t len,bool write)
{
    if(!len || ownership_is_worker || !retro_atomic_load_acquire_int(&ownership_ready)) return;
    if(retro_atomic_load_acquire_int(&ownership_done)==retro_atomic_load_acquire_int(&ownership_posted)) {
        ownership_relax_if_cpu();return;
    }
    MCPXAPUState *d=ownership_device;
    uint64_t base=memory_region_get_ram_addr(d->ram),size=memory_region_size(d->ram);
    if(addr>=base+size || addr+len<=base) return;
    uint64_t first=(MAX(addr,base)-base)/TARGET_PAGE_SIZE;
    uint64_t last=(MIN(addr+len,base+size)-base-1)/TARGET_PAGE_SIZE;
    /* The live map is CPU-owned and immutable while an epoch is pending. */
    for(uint64_t page=first;page<=last;page++) {
        if(ownership_page_live(page) && (write ||
            !(ownership_readonly_pages[page/64] & (UINT64_C(1)<<(page%64))))){
            lr_dsp_ownership_fence(LR_DSP_FENCE_RAM);return;
        }
    }
}
/* Unclassified borrowers retain write-strength protection. Reads may share
 * table pages only after admission proves no DMA target can alias them. */
void lr_dsp_ownership_ram(uint64_t addr,uint64_t len)
{ ownership_ram_access(addr,len,true); }
void lr_dsp_ownership_read(uint64_t addr,uint64_t len)
{ ownership_ram_access(addr,len,false); }
/* The option values the DSPs were last set up for: none on a new device
 * (mcpx_apu_dsp_init), so new content gets its monitor point and engine
 * whatever the last content ran with */
static int last_known_dsp_pref = -1;
static int last_known_jit_pref = -1;

void mcpx_apu_update_dsp_preference(MCPXAPUState *d)
{
    if (last_known_dsp_pref == (int)g_config.audio.use_dsp &&
        last_known_jit_pref == (int)g_config.audio.use_dsp_jit) {
        return;
    }
    /* Drain the old engine before changing its monitor or JIT state. */
    lr_dsp_ownership_quiesce();

    if (last_known_dsp_pref != (int)g_config.audio.use_dsp) {
        if (g_config.audio.use_dsp) {
            d->monitor.point = MCPX_APU_DEBUG_MON_GP_OR_EP;
            d->gp.realtime = true;
            d->ep.realtime = true;
        } else {
            d->monitor.point = MCPX_APU_DEBUG_MON_VP;
            d->gp.realtime = false;
            d->ep.realtime = false;
        }
        last_known_dsp_pref = g_config.audio.use_dsp;
    }

    if (last_known_jit_pref != (int)g_config.audio.use_dsp_jit) {
        dsp_set_engine(d->gp.dsp, g_config.audio.use_dsp_jit);
        dsp_set_engine(d->ep.dsp, g_config.audio.use_dsp_jit);
        last_known_jit_pref = g_config.audio.use_dsp_jit;
    }
}


/* Fixed maps cover Xbox and 128 MiB Chihiro RAM. Larger mappings decline
 * offload; the CPU path remains available. */
#define DSP_RAM_PAGES 32768
static uint64_t ownership_pages[DSP_RAM_PAGES / 64];
static uint64_t ownership_tables[DSP_RAM_PAGES / 64];

static retro_atomic_64_t own_watched[DSP_RAM_PAGES / 64];
static retro_atomic_64_t own_live[DSP_RAM_PAGES / 64];
static int ownership_page_live(uint64_t page)
{ return page<DSP_RAM_PAGES && ((uint64_t)retro_atomic_load_relaxed_64(&own_live[page/64])&(UINT64_C(1)<<(page%64)))!=0; }

static MemAccessCallback *own_callback;
static retro_atomic_int_t ownership_guards_armed;
static void ownership_relax_if_cpu(void)
{
    /* External borrowers can reach this too. Keep the cheap inactive check
     * atomic; only the CPU owner may inspect and retire the callback/TLB. */
    if(!retro_atomic_load_relaxed_int(&ownership_guards_armed)) return;
    CPUState *cpu=qemu_get_cpu(0);
    if(!cpu || !qemu_cpu_is_self(cpu)) return;
    retro_atomic_store_relaxed_int(&own_callback->watch_flags,0);
    retro_atomic_store_relaxed_int(&ownership_guards_armed,0);
    lr_dsp_tlb_refresh(cpu,false);
}
bool lr_dsp_tlb_watched(uint64_t addr)
{
    if(!retro_atomic_load_acquire_int(&ownership_ready)) return false;
    MCPXAPUState *d=ownership_device;
    uint64_t base=memory_region_get_ram_addr(d->ram),size=memory_region_size(d->ram);
    if(addr<base || addr-base>=size) return false;
    uint64_t page=(addr-base)/TARGET_PAGE_SIZE;
    if(page>=DSP_RAM_PAGES) return false;
    return ((uint64_t)retro_atomic_load_relaxed_64(&own_watched[page/64])&(UINT64_C(1)<<(page%64)))!=0;
}

bool lr_dsp_ownership_match(MemoryRegion *mr, hwaddr addr, hwaddr len)
{
    (void)mr;
    if (!len || addr >= (uint64_t)DSP_RAM_PAGES * TARGET_PAGE_SIZE) return false;
    uint64_t bytes = MIN(len, (uint64_t)DSP_RAM_PAGES * TARGET_PAGE_SIZE - addr);
    uint64_t last = (addr + bytes - 1) / TARGET_PAGE_SIZE;
    for (uint64_t p = addr / TARGET_PAGE_SIZE; p <= last; p++) {
        if ((uint64_t)retro_atomic_load_relaxed_64(&own_watched[p / 64]) & (UINT64_C(1) << (p % 64))) return true;
    }
    return false;
}
void lr_dsp_ownership_callback(void *opaque, MemoryRegion *mr, hwaddr addr, hwaddr len, bool write)
{
    (void)opaque;
    if (write) lr_dsp_ownership_ram(memory_region_get_ram_addr(mr)+addr,len);
    else lr_dsp_ownership_read(memory_region_get_ram_addr(mr)+addr,len);
}

static void own_protect(MCPXAPUState *d)
{
    bool changed = false;
    ownership_readonly_pages=ownership_tables;
    if (!own_callback) own_callback = mem_access_callback_insert(qemu_get_cpu(0), d->ram, 0,
        memory_region_size(d->ram), lr_dsp_ownership_callback, d);
    for(unsigned i = 0; i < DSP_RAM_PAGES / 64; i++) {
        uint64_t old = retro_atomic_load_relaxed_64(&own_watched[i]);
        changed |= (ownership_pages[i] & ~old) != 0;
        retro_atomic_store_relaxed_64(&own_watched[i], old | ownership_pages[i]);
        retro_atomic_store_relaxed_64(&own_live[i], ownership_pages[i]);
    }
    /* Called outside generated code. Existing translations must not retain
     * an unguarded RAM pointer when newly protected pages are published. */
    if (changed) tlb_flush(qemu_get_cpu(0));
    retro_atomic_store_relaxed_int(&own_callback->watch_flags,BP_MEM_READ|BP_MEM_WRITE);
    retro_atomic_store_relaxed_int(&ownership_guards_armed,1);
    lr_dsp_tlb_refresh(qemu_get_cpu(0),true);
}

/* Command-range tracking with nonblocking DSP admission. DSP
 * reservations last through job completion; GPU references last through
 * command retirement. Admission conflicts take the inline path. */
static retro_atomic_int_t gpu_page_refs[DSP_RAM_PAGES];
static retro_atomic_int_t gpu_range_unknown;
static struct {uintptr_t cmd;uint64_t bits[DSP_RAM_PAGES / 64];} gpu_commands[64];
void lr_gpu_ram_claim(uintptr_t cmd, uint64_t addr, uint64_t len)
{
    const uint64_t capacity = (uint64_t)DSP_RAM_PAGES * TARGET_PAGE_SIZE;
    if (!len) return;
    if (!cmd || addr >= capacity || len > capacity - addr) {
        retro_atomic_store_release_int(&gpu_range_unknown, 1);return;
    }
    unsigned slot;
    for(slot=0;slot<64;slot++) if(gpu_commands[slot].cmd==cmd) break;
    if(slot==64) for(slot=0;slot<64;slot++) if(!gpu_commands[slot].cmd) break;
    if(slot==64){retro_atomic_store_release_int(&gpu_range_unknown,1);return;}
    gpu_commands[slot].cmd=cmd;
    uint64_t last=(addr+len-1)/TARGET_PAGE_SIZE;
    for(uint64_t page=addr/TARGET_PAGE_SIZE;page<=last;page++) {
        uint64_t bit=UINT64_C(1)<<(page%64);
        if(!(gpu_commands[slot].bits[page/64]&bit)) {
            for (;;) {
                int n=retro_atomic_load_acquire_int(&gpu_page_refs[page]);
                if(n>=0) { if(retro_atomic_cas_int(&gpu_page_refs[page],n,n+1)) break; }
                else {
                    int key=retro_eventcount_prepare_wait(&gpu_ownership_changed);
                    if(retro_atomic_load_acquire_int(&gpu_page_refs[page])>=0)
                        retro_eventcount_cancel_wait(&gpu_ownership_changed);
                    else retro_eventcount_commit_wait(&gpu_ownership_changed,key);
                }
            }
            gpu_commands[slot].bits[page/64]|=bit;
        }
    }
}
void lr_gpu_ram_drop(uintptr_t cmd)
{
    for(unsigned slot=0;slot<64;slot++) if(gpu_commands[slot].cmd==cmd && cmd) {
        for(unsigned word=0;word<DSP_RAM_PAGES/64;word++) {
            uint64_t bits=gpu_commands[slot].bits[word];
            while(bits) {
                unsigned bit=__builtin_ctzll(bits);bits&=bits-1;
                int old=retro_atomic_fetch_sub_int(&gpu_page_refs[word*64+bit],1);
                assert(old>0);
            }
            gpu_commands[slot].bits[word]=0;
        }
        gpu_commands[slot].cmd=0;
        return;
    }
}

static uint64_t dsp_gpu_reserved[DSP_RAM_PAGES / 64];
static void gpu_release_dsp_pages(void)
{
    for(unsigned word=0;word<DSP_RAM_PAGES/64;word++) {
        uint64_t bits=dsp_gpu_reserved[word];
        while(bits) {
            unsigned bit=__builtin_ctzll(bits);bits&=bits-1;
            retro_atomic_store_release_int(&gpu_page_refs[word*64+bit],0);
        }
        dsp_gpu_reserved[word]=0;
    }
    retro_eventcount_notify(&gpu_ownership_changed);
}
static bool gpu_try_dsp_pages(void)
{
    if(retro_atomic_load_acquire_int(&gpu_range_unknown)) return false;
    for(unsigned word=0;word<DSP_RAM_PAGES/64;word++) {
        uint64_t bits=ownership_pages[word];
        while(bits) {
            unsigned bit=__builtin_ctzll(bits);bits&=bits-1;
            if(dsp_gpu_reserved[word]&(UINT64_C(1)<<bit)) continue;
            if(!retro_atomic_cas_int(&gpu_page_refs[word*64+bit],0,-1)) {
                gpu_release_dsp_pages();return false;
            }
            dsp_gpu_reserved[word]|=UINT64_C(1)<<bit;
        }
    }
    return true;
}
static void ownership_mark(uint64_t *pages, uint64_t addr, size_t len)
{
    if (!len) return;
    uint64_t last = (addr + len - 1) / TARGET_PAGE_SIZE;
    for (uint64_t page = addr / TARGET_PAGE_SIZE; page <= last; page++) {
        assert(page < DSP_RAM_PAGES);
        pages[page / 64] |= UINT64_C(1) << (page % 64);
    }
}

static void scatter_gather_walk(MCPXAPUState *d, hwaddr sge_base,
                                unsigned int max_sge, uint32_t addr,
                                size_t len, bool dir,
                                dsp_dma_run_func fn, void *user)
{
    unsigned int page_entry = addr / TARGET_PAGE_SIZE;
    unsigned int offset_in_page = addr % TARGET_PAGE_SIZE;
    unsigned int bytes_to_copy = TARGET_PAGE_SIZE - offset_in_page;

    while (len > 0) {
        assert(page_entry <= max_sge);

        uint32_t prd_address = ownership_is_worker ?
            ldl_le_p(d->ram_ptr+sge_base+page_entry*8) :
            ldl_le_phys(&address_space_memory,sge_base+page_entry*8);
        // uint32_t prd_control = ldl_le_phys(&address_space_memory,
        //                                     sge_base + page_entry * 8 + 4);

        hwaddr paddr = prd_address + offset_in_page;

        if (bytes_to_copy > len) {
            bytes_to_copy = len;
        }

        assert(paddr + bytes_to_copy < memory_region_size(d->ram));

        /* the page's run of the guest's RAM, in place */
        /* A CPU-owned GP may overlap an EP job. Guard its actual DMA
         * span before lending the direct RAM pointer to the transfer. */
        if (ownership_gp_dma_guard && !ownership_is_worker) {
            uint64_t ram_addr=memory_region_get_ram_addr(d->ram)+paddr;
            if (dir) lr_dsp_ownership_ram(ram_addr,bytes_to_copy);
            else lr_dsp_ownership_read(ram_addr,bytes_to_copy);
        }
        fn(user, &d->ram_ptr[paddr], bytes_to_copy);
        if (dir) {
            memory_region_set_dirty(d->ram, paddr, bytes_to_copy);
        }

        len -= bytes_to_copy;

        /* After the first iteration, we are page aligned */
        page_entry += 1;
        bytes_to_copy = TARGET_PAGE_SIZE;
        offset_in_page = 0;
    }
}

static void gp_scratch_walk(void *opaque, uint32_t addr, size_t len, bool dir,
                            dsp_dma_run_func fn, void *user)
{
    MCPXAPUState *d = opaque;
    // fprintf(stderr, "GP %s scratch 0x%x bytes (0x%x words) at %x (0x%x words)\n", dir ? "writing to" : "reading from", len, len/4, addr, addr/4);
    scatter_gather_walk(d, apu_reg_r(d, NV_PAPU_GPSADDR), apu_reg_r(d, NV_PAPU_GPSMAXSGE),
                        addr, len, dir, fn, user);
}

static void ep_scratch_walk(void *opaque, uint32_t addr, size_t len, bool dir,
                            dsp_dma_run_func fn, void *user)
{
    MCPXAPUState *d = opaque;
    // fprintf(stderr, "EP %s scratch 0x%x bytes (0x%x words) at %x (0x%x words)\n", dir ? "writing to" : "reading from", len, len/4, addr, addr/4);
    scatter_gather_walk(d, apu_reg_r(d, NV_PAPU_EPSADDR), apu_reg_r(d, NV_PAPU_EPSMAXSGE),
                        addr, len, dir, fn, user);
}

static uint32_t circular_scatter_gather_walk(MCPXAPUState *d, hwaddr sge_base,
                                             unsigned int max_sge,
                                             uint32_t base, uint32_t end,
                                             uint32_t cur, size_t len, bool dir,
                                             dsp_dma_run_func fn, void *user)
{
    while (len > 0) {
        unsigned int bytes_to_copy = end - cur;

        if (bytes_to_copy > len) {
            bytes_to_copy = len;
        }

        DPRINTF("circular scatter gather %s in range 0x%x - 0x%x at 0x%x of "
                "length 0x%x / 0x%lx bytes\n",
                dir ? "write" : "read", base, end, cur, bytes_to_copy, len);

        assert((cur >= base) && ((cur + bytes_to_copy) <= end));
        scatter_gather_walk(d, sge_base, max_sge, cur, bytes_to_copy, dir, fn, user);

        len -= bytes_to_copy;

        /* After the first iteration we might have to wrap */
        cur += bytes_to_copy;
        if (cur >= end) {
            assert(cur == end);
            cur = base;
        }
    }

    return cur;
}

static void gp_fifo_walk(void *opaque, unsigned int index, size_t len, bool dir,
                         dsp_dma_run_func fn, void *user)
{
    MCPXAPUState *d = opaque;
    uint32_t base;
    uint32_t end;
    hwaddr cur_reg;
    if (dir) {
        assert(index < GP_OUTPUT_FIFO_COUNT);
        base = GET_MASK(apu_reg_r(d, NV_PAPU_GPOFBASE0 + 0x10 * index),
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(apu_reg_r(d, NV_PAPU_GPOFEND0 + 0x10 * index),
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_GPOFCUR0 + 0x10 * index;
    } else {
        assert(index < GP_INPUT_FIFO_COUNT);
        base = GET_MASK(apu_reg_r(d, NV_PAPU_GPIFBASE0 + 0x10 * index),
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(apu_reg_r(d, NV_PAPU_GPIFEND0 + 0x10 * index),
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_GPIFCUR0 + 0x10 * index;
    }

    uint32_t cur = GET_MASK(apu_reg_r(d, cur_reg), NV_PAPU_GPOFCUR0_VALUE);

    // fprintf(stderr, "GP %s fifo #%d, base = %x, end = %x, cur = %x, len = %x\n",
    //     dir ? "writing to" : "reading from", index,
    //     base, end, cur, len);

    /* DSP hangs if current >= end; but forces current >= base */
    assert(cur < end);
    if (cur < base) {
        cur = base;
    }

    cur = circular_scatter_gather_walk(d,
        apu_reg_r(d, NV_PAPU_GPFADDR), apu_reg_r(d, NV_PAPU_GPFMAXSGE),
        base, end, cur, len, dir, fn, user);

    APU_REG_SET_MASK(d, cur_reg, NV_PAPU_GPOFCUR0_VALUE, cur);
}

/* the EP's output FIFO 0 sinks to the frontend: a run of the guest's
 * FIFO gets silence instead (ep_fifo_walk) */
static void ep_silence_run(void *user, uint8_t *host, size_t bytes)
{
    memset(host, 0, bytes);
}

static void ep_fifo_walk(void *opaque, unsigned int index, size_t len, bool dir,
                         dsp_dma_run_func fn, void *user)
{
    MCPXAPUState *d = opaque;
    uint32_t base;
    uint32_t end;
    hwaddr cur_reg;
    if (dir) {
        assert(index < EP_OUTPUT_FIFO_COUNT);
        base = GET_MASK(apu_reg_r(d, NV_PAPU_EPOFBASE0 + 0x10 * index),
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(apu_reg_r(d, NV_PAPU_EPOFEND0 + 0x10 * index),
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_EPOFCUR0 + 0x10 * index;
    } else {
        assert(index < EP_INPUT_FIFO_COUNT);
        base = GET_MASK(apu_reg_r(d, NV_PAPU_EPIFBASE0 + 0x10 * index),
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(apu_reg_r(d, NV_PAPU_EPIFEND0 + 0x10 * index),
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_EPIFCUR0 + 0x10 * index;
    }

    uint32_t cur = GET_MASK(apu_reg_r(d, cur_reg), NV_PAPU_GPOFCUR0_VALUE);

    // fprintf(stderr, "EP %s fifo #%d, base = %x, end = %x, cur = %x, len = %x\n",
    //     dir ? "writing to" : "reading from", index,
    //     base, end, cur, len);

    if (dir && index == 0 && d->monitor.point != MCPX_APU_DEBUG_MON_AC97) {
        /* sunk: the EP's 256 samples, 8 engine frames' worth, written
         * straight into the frontend's queue (dsp_run_frame), and the
         * guest's FIFO gets silence */
        if ((d->monitor.point == MCPX_APU_DEBUG_MON_EP) ||
            (d->monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP)) {
            assert(len == sizeof(d->dsp_state.ep_spare));
            fn(user, (uint8_t *)d->dsp_state.ep_dst, len);
            d->dsp_state.ep_written = true;
        }
        fn = ep_silence_run;
    }

    /* DSP hangs if current >= end; but forces current >= base */
    if (cur >= end) {
        cur = cur % (end - base);
    }
    if (cur < base) {
        cur = base;
    }

    cur = circular_scatter_gather_walk(d,
        apu_reg_r(d, NV_PAPU_EPFADDR), apu_reg_r(d, NV_PAPU_EPFMAXSGE),
        base, end, cur, len, dir, fn, user);

    APU_REG_SET_MASK(d, cur_reg, NV_PAPU_GPOFCUR0_VALUE, cur);
}

static void proc_rst_write(DSPState *dsp, uint32_t oldval, uint32_t val)
{
    if (!(val & NV_PAPU_GPRST_GPRST) || !(val & NV_PAPU_GPRST_GPDSPRST)) {
        dsp_reset(dsp);
    } else if (
        (!(oldval & NV_PAPU_GPRST_GPRST) || !(oldval & NV_PAPU_GPRST_GPDSPRST))
        && ((val & NV_PAPU_GPRST_GPRST) && (val & NV_PAPU_GPRST_GPDSPRST))) {
        dsp_bootstrap(dsp);
    }
}

/* Whether a GP/EP offset is in one of the DSP's memories */
static bool gp_is_memory(hwaddr addr)
{
    return addr < NV_PAPU_GPXMEM + 0x1000 * 4 ||
           (addr >= NV_PAPU_GPMIXBUF && addr < NV_PAPU_GPMIXBUF + 0x400 * 4) ||
           (addr >= NV_PAPU_GPYMEM && addr < NV_PAPU_GPYMEM + 0x800 * 4) ||
           (addr >= NV_PAPU_GPPMEM && addr < NV_PAPU_GPPMEM + 0x1000 * 4);
}

static bool ep_is_memory(hwaddr addr)
{
    return addr < NV_PAPU_EPXMEM + 0xC00 * 4 ||
           (addr >= NV_PAPU_EPYMEM && addr < NV_PAPU_EPYMEM + 0x100 * 4) ||
           (addr >= NV_PAPU_EPPMEM && addr < NV_PAPU_EPPMEM + 0x1000 * 4);
}

/* Global Processor - programmable DSP */
static uint64_t gp_read(void *opaque, hwaddr addr, unsigned int size)
{
    lr_dsp_ownership_quiesce();
    MCPXAPUState *d = opaque;

    assert(size == 4);
    assert(addr % 4 == 0);

    uint64_t r = 0;
    switch (addr) {
    case NV_PAPU_GPXMEM ... NV_PAPU_GPXMEM + 0x1000 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPXMEM) / 4;
        r = dsp_read_memory(d->gp.dsp, 'X', xaddr);
        // fprintf(stderr, "read GP NV_PAPU_GPXMEM [%x] -> %x\n", xaddr, r);
        break;
    }
    case NV_PAPU_GPMIXBUF ... NV_PAPU_GPMIXBUF + 0x400 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPMIXBUF) / 4;
        r = dsp_read_memory(d->gp.dsp, 'X', GP_DSP_MIXBUF_BASE + xaddr);
        // fprintf(stderr, "read GP NV_PAPU_GPMIXBUF [%x] -> %x\n", xaddr, r);
        break;
    }
    case NV_PAPU_GPYMEM ... NV_PAPU_GPYMEM + 0x800 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_GPYMEM) / 4;
        r = dsp_read_memory(d->gp.dsp, 'Y', yaddr);
        // fprintf(stderr, "read GP NV_PAPU_GPYMEM [%x] -> %x\n", yaddr, r);
        break;
    }
    case NV_PAPU_GPPMEM ... NV_PAPU_GPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_GPPMEM) / 4;
        r = dsp_read_memory(d->gp.dsp, 'P', paddr);
        // fprintf(stderr, "read GP NV_PAPU_GPPMEM [%x] -> %x\n", paddr, r);
        break;
    }
    default:
        /* the guest's view, the engine's own (gp_write) */
        r = d->gp.regs[addr];
        break;
    }
    DPRINTF("mcpx apu GP: read [0x%" HWADDR_PRIx "] -> 0x%lx\n", addr, r);

    return r;
}

/* A GP write as the DSP takes it, in order with its frames: on its
 * thread, or on the emulator thread with that one idle */
static void gp_apply(MCPXAPUState *d, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case NV_PAPU_GPXMEM ... NV_PAPU_GPXMEM + 0x1000 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPXMEM) / 4;
        dsp_write_memory(d->gp.dsp, 'X', xaddr, val);
        break;
    }
    case NV_PAPU_GPMIXBUF ... NV_PAPU_GPMIXBUF + 0x400 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPMIXBUF) / 4;
        dsp_write_memory(d->gp.dsp, 'X', GP_DSP_MIXBUF_BASE + xaddr, val);
        break;
    }
    case NV_PAPU_GPYMEM ... NV_PAPU_GPYMEM + 0x800 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_GPYMEM) / 4;
        dsp_write_memory(d->gp.dsp, 'Y', yaddr, val);
        break;
    }
    case NV_PAPU_GPPMEM ... NV_PAPU_GPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_GPPMEM) / 4;
        dsp_write_memory(d->gp.dsp, 'P', paddr, val);
        break;
    }
    case NV_PAPU_GPRST:
        proc_rst_write(d->gp.dsp, d->dsp_state.gp_rst, val);
        d->dsp_state.gp_rst = val;
        break;
    default:
        break;
    }
}

static void gp_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    lr_dsp_ownership_quiesce();
    MCPXAPUState *d = opaque;

    assert(size == 4);
    assert(addr % 4 == 0);

    DPRINTF("mcpx apu GP: [0x%" HWADDR_PRIx "] = 0x%lx\n", addr, val);

    /* the registers are the guest's view, at once; the memories and the
     * reset go to the DSP in order with its frames (gp_apply) */
    if (!gp_is_memory(addr)) {
        d->gp.regs[addr] = val;
    }
    if (gp_is_memory(addr) || addr == NV_PAPU_GPRST) {
        mcpx_apu_dsp_write(d, MCPX_APU_DSP_WRITE_GP, addr, val);
    }
}

const MemoryRegionOps gp_ops = {
    .read = gp_read,
    .write = gp_write,
};

/* Encode Processor - encoding DSP */
static uint64_t ep_read(void *opaque, hwaddr addr, unsigned int size)
{
    lr_dsp_ownership_quiesce();
    MCPXAPUState *d = opaque;

    assert(size == 4);
    assert(addr % 4 == 0);

    uint64_t r = 0;
    switch (addr) {
    case NV_PAPU_EPXMEM ... NV_PAPU_EPXMEM + 0xC00 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_EPXMEM) / 4;
        r = dsp_read_memory(d->ep.dsp, 'X', xaddr);
        // fprintf(stderr, "read EP  NV_PAPU_EPXMEM [%x] -> %x\n", xaddr, r);
        break;
    }
    case NV_PAPU_EPYMEM ... NV_PAPU_EPYMEM + 0x100 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_EPYMEM) / 4;
        r = dsp_read_memory(d->ep.dsp, 'Y', yaddr);
        // fprintf(stderr, "read EP  NV_PAPU_EPYMEM [%x] -> %x\n", yaddr, r);
        break;
    }
    case NV_PAPU_EPPMEM ... NV_PAPU_EPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_EPPMEM) / 4;
        r = dsp_read_memory(d->ep.dsp, 'P', paddr);
        // fprintf(stderr, "read EP  NV_PAPU_EPPMEM [%x] -> %x\n", paddr, r);
        break;
    }
    default:
        /* the guest's view, the engine's own (ep_write) */
        r = d->ep.regs[addr];
        break;
    }
    DPRINTF("mcpx apu EP: read [0x%" HWADDR_PRIx "] -> 0x%lx\n", addr, r);

    return r;
}

/* An EP write as the DSP takes it (gp_apply) */
static void ep_apply(MCPXAPUState *d, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case NV_PAPU_EPXMEM ... NV_PAPU_EPXMEM + 0xC00 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_EPXMEM) / 4;
        dsp_write_memory(d->ep.dsp, 'X', xaddr, val);
        break;
    }
    case NV_PAPU_EPYMEM ... NV_PAPU_EPYMEM + 0x100 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_EPYMEM) / 4;
        dsp_write_memory(d->ep.dsp, 'Y', yaddr, val);
        break;
    }
    case NV_PAPU_EPPMEM ... NV_PAPU_EPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_EPPMEM) / 4;
        dsp_write_memory(d->ep.dsp, 'P', paddr, val);
        break;
    }
    case NV_PAPU_EPRST:
        proc_rst_write(d->ep.dsp, d->dsp_state.ep_rst, val);
        d->dsp_state.ep_rst = val;
        break;
    default:
        break;
    }
}

static void ep_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    lr_dsp_ownership_quiesce();
    MCPXAPUState *d = opaque;

    assert(size == 4);
    assert(addr % 4 == 0);

    DPRINTF("mcpx apu EP: [0x%" HWADDR_PRIx "] = 0x%lx\n", addr, val);

    /* as gp_write; the engine's count of the EP's frames starts over at
     * once, the frames posted from here on taking it (mcpx_apu_dsp_post) */
    if (!ep_is_memory(addr)) {
        d->ep.regs[addr] = val;
    }
    if (ep_is_memory(addr) || addr == NV_PAPU_EPRST) {
        mcpx_apu_dsp_write(d, MCPX_APU_DSP_WRITE_EP, addr, val);
    }
    if (addr == NV_PAPU_EPRST) {
        d->ep_frame_div = 0; /* FIXME: Still unsure about frame sync */
    }
}

const MemoryRegionOps ep_ops = {
    .read = ep_read,
    .write = ep_write,
};

/* GP stage: convert the VP mix directly into GP memory and execute GP.
 * Split mode keeps this stage on the CPU; its DMA guards order any
 * shared guest RAM against the pending EP job. Queue access is fenced. */
static void dsp_run_gp(MCPXAPUState *d, int64_t mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    bool previous_guard=ownership_gp_dma_guard;
    if (ownership_enabled) {
        /* The CPU cannot post another EP job until this GP stage returns. */
        ownership_gp_dma_guard=!ownership_is_worker &&
            retro_atomic_load_acquire_int(&ownership_ready) &&
            retro_atomic_load_acquire_int(&ownership_posted)!=retro_atomic_load_acquire_int(&ownership_done);
    }
    /* Write VP results to the GP DSP MIXBUF, in place */
    uint32_t *mixbuf = dsp_memory_ptr(d->gp.dsp, 'X', GP_DSP_MIXBUF_BASE,
                                      NUM_MIXBINS * NUM_SAMPLES_PER_FRAME);
    const int64_t *in = &mixbins[0][0];
    for (int i = 0; i < NUM_MIXBINS * NUM_SAMPLES_PER_FRAME; i++) {
        mixbuf[i] = (uint32_t)vp_mix_to_24(in[i]) & 0xffffff;
    }

    uint32_t ep_rst = d->dsp_state.ep_rst, gp_rst = d->dsp_state.gp_rst;
    bool ep_enabled = (ep_rst & NV_PAPU_GPRST_GPRST) &&
                      (ep_rst & NV_PAPU_GPRST_GPDSPRST);

    bool gp_enabled = (gp_rst & NV_PAPU_GPRST_GPRST) &&
                      (gp_rst & NV_PAPU_GPRST_GPDSPRST);
    bool gp_out = (d->monitor.point == MCPX_APU_DEBUG_MON_GP) ||
                  (d->monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP && !ep_enabled);

    /* Run GP */
    if (gp_enabled) {
        dsp_start_frame(d->gp.dsp);
        dsp_set_halt_requested(d->gp.dsp, false);
        dsp_set_cycle_count(d->gp.dsp, 0);
        if (d->gp.realtime) {
            dsp_run_until_halt(d->gp.dsp);
        } else {
            dsp_run(d->gp.dsp, 1000);
        }

        if (gp_out) {
                /* mixbins 0 and 1, straight into the frontend's queue */
            int16_t *out = mcpx_apu_monitor_begin(NUM_SAMPLES_PER_FRAME);
            if (out) {
                const int32_t gain = mcpx_apu_monitor_gain_q16();
                const uint32_t *l = &mixbuf[0];
                const uint32_t *r = &mixbuf[NUM_SAMPLES_PER_FRAME];
                for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                    out[2 * i] = mcpx_apu_monitor_scale_s16((int16_t)(l[i] >> 8), gain);
                    out[2 * i + 1] = mcpx_apu_monitor_scale_s16((int16_t)(r[i] >> 8), gain);
                }
                mcpx_apu_monitor_end(NUM_SAMPLES_PER_FRAME);
            }
        }
    } else if (gp_out) {
        mcpx_apu_monitor_push_silence();
    }

    if (ownership_enabled) ownership_gp_dma_guard=previous_guard;
}

static void dsp_run_ep(MCPXAPUState *d, int frame_div)
{
    const uint32_t running=NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    bool ep_enabled=(d->dsp_state.ep_rst & running)==running;
    bool ep_out = (d->monitor.point == MCPX_APU_DEBUG_MON_EP) ||
                  (d->monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP && ep_enabled);

    /* Run EP: its 256 samples are this frame's and the next seven's. Its
     * output DMA writes them straight into the frontend's queue, where
     * they are committed at once; the next seven frames then skip theirs
     * (monitor.c). Silence where its program made none. */
    if (ep_enabled && frame_div % 8 == 0) {
        MCPXAPUDSPState *t = &d->dsp_state;
        int16_t *q = ep_out ? mcpx_apu_monitor_begin_ahead(256) : NULL;

        t->ep_dst = q ? (int16_t (*)[2])q : t->ep_spare;
        t->ep_written = false;
        dsp_start_frame(d->ep.dsp);
        dsp_set_halt_requested(d->ep.dsp, false);
        dsp_set_cycle_count(d->ep.dsp, 0);
        if (d->ep.realtime) {
            dsp_run_until_halt(d->ep.dsp);
        } else {
            dsp_run(d->ep.dsp, 1000);
        }
        if (q) {
            if (!t->ep_written) {
                memset(q, 0, 256 * 2 * sizeof(int16_t));
            }
            mcpx_apu_monitor_end_ahead(q, 256);
            return;
        }
    }

    if (ep_out) {
        /* none of the EP's output queued for this frame (the EP off or
         * started mid-cycle, a state loaded, the queue full): silence,
         * unless an earlier frame queued it ahead */
        mcpx_apu_monitor_push_silence();
    }
}

void mcpx_apu_dsp_write(MCPXAPUState *d, uint32_t kind, hwaddr addr, uint32_t val)
{
    switch (kind) {
    case MCPX_APU_DSP_WRITE_GP: gp_apply(d, addr, val); break;
    case MCPX_APU_DSP_WRITE_EP: ep_apply(d, addr, val); break;
    default: apu_reg_w(d, addr, val); break;
    }
}

void mcpx_apu_dsp_sync_regs(MCPXAPUState *d)
{
    d->dsp_state.gp_rst = d->gp.regs[NV_PAPU_GPRST];
    d->dsp_state.ep_rst = d->ep.regs[NV_PAPU_EPRST];
}

int64_t (*mcpx_apu_dsp_mixbins(MCPXAPUState *d))[NUM_SAMPLES_PER_FRAME]
{
    d->dsp_vp_pending = retro_atomic_load_acquire_int(&ownership_ready) &&
        retro_atomic_load_acquire_int(&ownership_posted) !=
        retro_atomic_load_acquire_int(&ownership_done);
    d->dsp_vp_pages = own_live;
    d->dsp_vp_readonly = ownership_tables;
    return d->dsp_state.mixbins;
}

static bool ownership_candidate(MCPXAPUState *d)
{
    static uint64_t targets[DSP_RAM_PAGES/64];
    const unsigned base[]={NV_PAPU_EPSADDR,NV_PAPU_EPFADDR};
    const unsigned limit[]={NV_PAPU_EPSMAXSGE,NV_PAPU_EPFMAXSGE};
    uint64_t size=memory_region_size(d->ram);
    memset(targets,0,sizeof(targets));
    for(unsigned t=0;t<2;t++) {
        uint64_t b=apu_reg_r(d,base[t]), n=(uint64_t)apu_reg_r(d,limit[t])+1;
        if(n>DSP_RAM_PAGES || b>=size || n*8>size-b) return false;
        for(unsigned e=0;e<n;e++) {
            uint64_t addr=ldl_le_p(d->ram_ptr+b+e*8);
            /* Invalid destinations cannot be accessed by the production
             * walker, which checks bounds before lending a RAM pointer. */
            if(addr>=size || TARGET_PAGE_SIZE>size-addr) continue;
            /* The reserved tables cannot change during this traversal.
             * Build the ownership set here instead of reading them twice. */
            ownership_mark(ownership_pages,addr,TARGET_PAGE_SIZE);
            for(uint64_t p=addr/TARGET_PAGE_SIZE;p<=(addr+TARGET_PAGE_SIZE-1)/TARGET_PAGE_SIZE;p++)
                targets[p/64]|=UINT64_C(1)<<(p%64);
        }
    }
    ram_addr_t ram_base=memory_region_get_ram_addr(d->ram);
    for(unsigned word=0;word<DSP_RAM_PAGES/64;word++) {
        if(targets[word]&ownership_tables[word]) return false;
        uint64_t bits=targets[word];
        while(bits) {
            unsigned bit=__builtin_ctzll(bits);bits&=bits-1;
            if(!physical_memory_get_dirty_flag(ram_base+(word*64+bit)*TARGET_PAGE_SIZE,DIRTY_MEMORY_CODE)) return false;
        }
    }
    /* GPU-owned surfaces must be settled on the CPU before a worker may
     * bypass their callback. Until that path is supported, decline overlap. */
    MemAccessCallback *cb;
    QTAILQ_FOREACH(cb,&qemu_get_cpu(0)->mem_access_callbacks,entry) {
        if(cb==own_callback || !retro_atomic_load_relaxed_int(&cb->watch_flags)) continue;
        if(cb->addr>=ram_base+size || cb->addr+cb->len<=ram_base) continue;
        uint64_t first=(MAX(cb->addr,ram_base)-ram_base)/TARGET_PAGE_SIZE;
        uint64_t last=(MIN(cb->addr+cb->len,ram_base+size)-ram_base-1)/TARGET_PAGE_SIZE;
        for(uint64_t page=first;page<=last;page++)
            if(ownership_pages[page/64]&(UINT64_C(1)<<(page%64))) return false;
    }
    return true;
}

static bool ownership_reserve_tables(MCPXAPUState *d)
{
    const unsigned base[]={NV_PAPU_EPSADDR,NV_PAPU_EPFADDR};
    const unsigned limit[]={NV_PAPU_EPSMAXSGE,NV_PAPU_EPFMAXSGE};
    uint64_t size=memory_region_size(d->ram);
    if(size>(uint64_t)DSP_RAM_PAGES*TARGET_PAGE_SIZE) return false;
    for(unsigned t=0;t<2;t++) {
        uint64_t b=apu_reg_r(d,base[t]),n=(uint64_t)apu_reg_r(d,limit[t])+1;
        if(n>DSP_RAM_PAGES || b>=size || n*8>size-b) return false;
        ownership_mark(ownership_pages,b,n*8);ownership_mark(ownership_tables,b,n*8);
    }
    return gpu_try_dsp_pages();
}
void mcpx_apu_dsp_post(MCPXAPUState *d,
                       int64_t mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    const uint32_t running = NV_PAPU_GPRST_GPRST | NV_PAPU_GPRST_GPDSPRST;
    bool split = ownership_enabled &&
        d->monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP &&
        (d->dsp_state.ep_rst & running) == running;
    /* Other monitor modes can produce GP audio: hand ownership back first.
     * Normal GP computation overlaps EP; actual DMA hazards wait in place. */
    if (!split) lr_dsp_ownership_quiesce();
    dsp_run_gp(d, mixbins);
    lr_dsp_ownership_fence(LR_DSP_FENCE_TICK);
    if (split && (d->ep_frame_div & 7) == 0 && !ownership_cpu_depth &&
        retro_atomic_load_acquire_int(&ownership_ready) &&
        bus_lease_try_dsp(&bus_lease)) {
        memset(ownership_pages, 0, sizeof(ownership_pages));
        memset(ownership_tables, 0, sizeof(ownership_tables));
        if (ownership_reserve_tables(d) && ownership_candidate(d) &&
            gpu_try_dsp_pages()) {
            own_protect(d);
            ownership_div = d->ep_frame_div;
            int epoch = (int)((unsigned)retro_atomic_load_relaxed_int(&ownership_posted) + 1u);
            retro_atomic_store_release_int(&ownership_posted, epoch);
            retro_eventcount_notify(&ownership_work);
            return;
        }
        gpu_release_dsp_pages();
        bus_lease_dsp_done(&bus_lease);
    }
    dsp_run_ep(d, d->ep_frame_div);
}

void mcpx_apu_dsp_post_silence(MCPXAPUState *d)
{
    (void)d;
    mcpx_apu_monitor_push_silence();
}

bool mcpx_apu_dsp_uses_reg(hwaddr addr)
{
    /* the scatter-gather tables and FIFOs (gp_scratch_walk, ep_scratch_walk,
     * gp_fifo_walk, ep_fifo_walk), and the EP's output FIFO cursors it moves */
    return (addr >= NV_PAPU_GPSADDR && addr <= NV_PAPU_EPFADDR + 3) ||
           (addr >= NV_PAPU_GPSMAXSGE && addr <= NV_PAPU_EPFMAXSGE + 3) ||
           (addr >= NV_PAPU_GPOFBASE0 && addr <= NV_PAPU_GPIFCUR1 + 3) ||
           (addr >= NV_PAPU_EPOFBASE0 && addr <= NV_PAPU_EPIFCUR0 + 3);
}

void mcpx_apu_dsp_init(MCPXAPUState *d)
{
    last_known_dsp_pref = -1;
    last_known_jit_pref = -1;
    ownership_start(d);
    d->gp.dsp = dsp_init(d, gp_scratch_walk, gp_fifo_walk, true);
    dsp_set_halt_requested(d->gp.dsp, false);
    dsp_set_cycle_count(d->gp.dsp, 0);

    d->ep.dsp = dsp_init(d, ep_scratch_walk, ep_fifo_walk, false);
    dsp_set_halt_requested(d->ep.dsp, false);
    dsp_set_cycle_count(d->ep.dsp, 0);

    /* Until DSP is more performant, a switch to decide whether or not we should
     * use the full audio pipeline or not.
     */
    mcpx_apu_update_dsp_preference(d);
}

/* The owning engine has stopped before device teardown. */
void mcpx_apu_dsp_finalize(MCPXAPUState *d)
{
    ownership_finish();
    mem_access_callback_remove_by_ref(qemu_get_cpu(0), own_callback);
    own_callback = NULL;
    retro_atomic_store_relaxed_int(&ownership_guards_armed,0);
    dsp_destroy(d->gp.dsp);
    dsp_destroy(d->ep.dsp);
    d->gp.dsp = NULL;
    d->ep.dsp = NULL;
}

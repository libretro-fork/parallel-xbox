/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "nv2a_int.h"
#include "pgraph/sun-shadow-hooks.h"
#include "pgraph/sun-shadow-fifo.h"
#include "cpu.h"
#include "system/address-spaces.h"
#include "exec/target_page.h"
#include "exec/tb-flush.h"

bool xemu_sun_camera_enabled;
static PgraphSunFifo *camera_fifo;

static bool camera_physical(void *opaque, uint32_t address, uint32_t *out)
{
    CPUState *cpu = opaque;
    hwaddr page = cpu_get_phys_page_debug(cpu, address & TARGET_PAGE_MASK);
    if (page == (hwaddr)-1) return false;
    hwaddr physical = page + (address & ~TARGET_PAGE_MASK);
    if (physical > UINT32_MAX || !g_nv2a || physical >= memory_region_size(g_nv2a->vram)) return false;
    MemoryRegionSection section = memory_region_find(get_system_memory(), physical, 4);
    bool ram = section.mr && memory_region_is_ram(section.mr) && int128_get64(section.size) >= 4;
    if (section.mr) memory_region_unref(section.mr);
    if (ram) *out = physical;
    return ram;
}

static bool camera_read(void *opaque, uint32_t address, void *out, size_t size)
{
    CPUState *cpu = opaque;
    if (!out || size > (uint64_t)UINT32_MAX + 1 - address) return false;
    while (size) {
        uint32_t physical;
        size_t amount = MIN(size, TARGET_PAGE_SIZE - (address & ~TARGET_PAGE_MASK));
        if (!camera_physical(cpu, address, &physical)) return false;
        MemoryRegionSection section = memory_region_find(get_system_memory(), physical, amount);
        bool ram = section.mr && memory_region_is_ram(section.mr) && int128_get64(section.size) >= amount;
        if (section.mr) memory_region_unref(section.mr);
        if (!ram || cpu_memory_rw_debug(cpu, address, out, amount, false)) return false;
        address += amount; out = (uint8_t *)out + amount; size -= amount;
    }
    return true;
}

static void camera_flush_translations(CPUState *cpu, run_on_cpu_data data)
{
    (void)cpu; (void)data;
    tb_flush__exclusive_or_serial();
}

bool xemu_sun_camera_attach(PgraphSunFifo *fifo)
{
    CPUState *cpu = first_cpu;
    if (camera_fifo || !cpu || !fifo || !fifo->storage || fifo->failed || !fifo->lifetime ||
        fifo->lifetime == UINT64_MAX || fifo->count || fifo->sequence ||
        !pgraph_sun_validate(&pgraph_sun_doaxvb_us, camera_read, cpu) ||
        !pgraph_sun_validate_draw_seams(camera_read, cpu)) return false;
    camera_fifo = fifo;
    xemu_sun_camera_enabled = true;
    /* The frame owner is outside current_cpu between slices, even while
     * runstate is Running. Queue the flush through the normal vCPU work path
     * so it runs with the CPU current, before translated execution resumes. */
    async_run_on_cpu(cpu, camera_flush_translations, RUN_ON_CPU_NULL);
    return true;
}

void xemu_sun_camera_detach(void)
{
    bool was_enabled = xemu_sun_camera_enabled;
    xemu_sun_camera_enabled = false;
    camera_fifo = NULL;
    if (was_enabled && first_cpu) async_run_on_cpu(first_cpu, camera_flush_translations, RUN_ON_CPU_NULL);
}

void xemu_sun_camera_capture(CPUState *cpu, uint64_t pc, uint32_t cursor)
{
    if (!camera_fifo || !g_nv2a || (pc != PGRAPH_SUN_DRAW_UP_PC && pc != PGRAPH_SUN_DRAW_INDEXED_PC)) return;
    /* Before the guest's header store/PUT publication. This lock also orders
     * the owned marker with the enhancement pusher; no mapping pointer is lent. */
    pfifo_guest_lock(g_nv2a);
    pgraph_sun_fifo_publish(camera_fifo, cursor, camera_read, camera_physical, cpu);
    qemu_mutex_unlock(&g_nv2a->pfifo.lock);
}

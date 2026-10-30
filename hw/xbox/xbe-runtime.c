/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/xbe-runtime.h"
#include "cpu.h"
#include "system/block-backend.h"
#include "block/libretro-disk.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "system/cpus.h"
#include "exec/target_page.h"
#include "exec/tb-flush.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"

uint32_t xemu_xbe_runtime_entry;
bool xemu_xbe_runtime_enabled = true;

static const uint8_t *disc_bytes(BlockBackend *blk, uint64_t offset,
                                  size_t size, uint8_t *scratch)
{
    if (offset > INT64_MAX) {
        return NULL;
    }
    const uint8_t *view = lr_disk_view(blk, offset, size);
    return view ? view :
        (blk_pread(blk, offset, size, scratch, 0) < 0 ? NULL : scratch);
}

static uint32_t disc_entry(BlockBackend *blk)
{
    uint8_t vbuf[2048], nbuf[14], name[256], hbuf[0x178], cbuf[0x1dc];
    const uint8_t *volume = blk ? disc_bytes(blk, 65536, sizeof(vbuf), vbuf) : NULL;
    if (!volume ||
        memcmp(volume, "MICROSOFT*XBOX*MEDIA", 20) ||
        memcmp(volume + 2028, "MICROSOFT*XBOX*MEDIA", 20)) {
        return 0;
    }
    uint64_t root = (uint64_t)ldl_le_p(volume + 20) * 2048;
    uint32_t size = ldl_le_p(volume + 24), offset = 0;
    if (size < 14 || size > 8 * 1024 * 1024) {
        return 0;
    }
    for (unsigned steps = 0; steps < 64; steps++) {
        const uint8_t *node;
        if (offset > size - 14 ||
            !(node = disc_bytes(blk, root + offset, sizeof(nbuf), nbuf)) ||
            node[13] > size - offset - 14 ||
            blk_pread(blk, root + offset + 14, node[13], name, 0) < 0) {
            return 0;
        }
        name[node[13]] = 0;
        int compare = g_ascii_strcasecmp("default.xbe", (char *)name);
        if (!compare) {
            uint64_t file = (uint64_t)ldl_le_p(node + 4) * 2048;
            uint32_t bytes = ldl_le_p(node + 8);
            const uint8_t *h, *cert;
            if (node[12] & 0x10 || bytes < sizeof(hbuf) ||
                !(h = disc_bytes(blk, file, sizeof(hbuf), hbuf))) {
                return 0;
            }
            uint32_t c = ldl_le_p(h + 0x118);
            uint32_t headers = ldl_le_p(h + 0x108);
            if (ldl_le_p(h + 0x104) != 0x10000 || c < 0x10178 ||
                headers > bytes || c - 0x10000 > bytes ||
                bytes - (c - 0x10000) < sizeof(cbuf) ||
                !(cert = disc_bytes(blk, file + c - 0x10000, sizeof(cbuf), cbuf))) {
                return 0;
            }
            XbeRuntimePlan plan;
            return xbe_runtime_plan(h, sizeof(hbuf), cert, sizeof(cbuf), false, &plan) ?
                   plan.entry : 0;
        }
        uint32_t child = lduw_le_p(node + (compare < 0 ? 0 : 2));
        if (!child) {
            return 0;
        }
        offset = child * 4;
    }
    return 0;
}

void xemu_xbe_runtime_disc(BlockBackend *blk)
{
    uint32_t entry = disc_entry(blk);
    if (entry != xemu_xbe_runtime_entry) {
        xemu_xbe_runtime_entry = entry;
        /* TBs with this entry always get the helper, even if the header is
         * not loaded yet. Flush old code when changing content so cached
         * code from another title cannot bypass the execution-time check.
         * Called on initial attachment or with the sole vCPU stopped. */
        if (first_cpu) {
            tb_flush__exclusive_or_serial();
        }
    }
}

static bool guest_ram(CPUState *cpu, uint32_t address, uint8_t *data,
                       size_t size, bool write)
{
    while (size) {
        hwaddr phys = cpu_get_phys_page_debug(cpu, address & TARGET_PAGE_MASK);
        size_t amount = MIN(size, TARGET_PAGE_SIZE - (address & ~TARGET_PAGE_MASK));
        if (phys == (hwaddr)-1) {
            return false;
        }
        phys += address & ~TARGET_PAGE_MASK;
        MemoryRegionSection section = memory_region_find(get_system_memory(),
                                                          phys, amount);
        bool ram = section.mr && memory_region_is_ram(section.mr) &&
                   int128_get64(section.size) >= amount;
        if (section.mr) {
            memory_region_unref(section.mr);
        }
        if (!ram || cpu_memory_rw_debug(cpu, address, data, amount, write) != 0) {
            return false;
        }
        address += amount;
        data += amount;
        size -= amount;
    }
    return true;
}

void xemu_xbe_runtime_apply(CPUState *cpu, uint64_t pc)
{
    uint8_t h[0x178], cert[0x1dc], flags[4];
    XbeRuntimePlan plan;
    if (!xemu_xbe_runtime_enabled || pc != xemu_xbe_runtime_entry ||
        !guest_ram(cpu, 0x10000, h, sizeof(h), false)) {
        return;
    }
    uint32_t c = ldl_le_p(h + 0x118), headers = ldl_le_p(h + 0x108);
    if (headers > 32768 || c < 0x10178 || c > 0x10000 + headers ||
        0x10000 + headers - c < sizeof(cert) ||
        !guest_ram(cpu, c, cert, sizeof(cert), false) ||
        !xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), true, &plan) ||
        pc != plan.entry) {
        return;
    }
    uint32_t value = ldl_le_p(cert + 0x1d8);
    if (!(value & 1)) {
        return;
    }
    stl_le_p(flags, value & ~1u);
    if (guest_ram(cpu, plan.flags, flags, sizeof(flags), true)) {
        info_report("Otogi: runtime disc-check compatibility applied "
                    "to loaded XBE (%08x -> %08x); image unchanged",
                    value, value & ~1u);
    }
}

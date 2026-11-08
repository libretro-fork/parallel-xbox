/*
 * xemu XBE accessing
 *
 * Helper functions to get details about the currently running executable.
 *
 * Copyright (C) 2020-2021 Matt Borgerson
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xemu-xbe.h"
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "system/hw_accel.h"
#include "cpu.h"
#include "exec/target_page.h"
#include "system/address-spaces.h"
#include "hw/xbox/nv2a/pgraph/sun-shadow-profile.h"

static int virt_to_phys(vaddr vaddr, hwaddr *phys_addr)
{
    MemTxAttrs attrs;
    CPUState *cs;
    hwaddr gpa;

    cs = qemu_get_cpu(0);
    if (!cs) {
        return 1; // No cpu
    }

    cpu_synchronize_state(cs);

    gpa = cpu_get_phys_page_attrs_debug(cs, vaddr & TARGET_PAGE_MASK, &attrs);
    if (gpa == -1) {
        return 1; // Unmapped
    } else {
        *phys_addr = gpa + (vaddr & ~TARGET_PAGE_MASK);
    }

    return 0;
}

static ssize_t virt_dma_memory_read(vaddr vaddr, void *buf, size_t len)
{
    size_t num_bytes_read = 0;

    while (num_bytes_read < len) {
        // Get physical page for this offset
        hwaddr phys_addr = 0;
        if (virt_to_phys(vaddr + num_bytes_read, &phys_addr) != 0) {
            return -1;
        }

        // Read contents from the page
        size_t bytes_remaining_in_page = TARGET_PAGE_SIZE - (phys_addr & ~TARGET_PAGE_MASK);
        size_t num_bytes_to_read = MIN(len - num_bytes_read, bytes_remaining_in_page);

        // FIXME: Check return value
        dma_memory_read(&address_space_memory,
                        phys_addr,
                        buf + num_bytes_read,
                        num_bytes_to_read,
                        MEMTXATTRS_UNSPECIFIED);

        num_bytes_read += num_bytes_to_read;
    }

    return num_bytes_read;
}

bool xemu_read_virtual_ram(uint32_t address, void *data, size_t size)
{
    CPUState *cpu = qemu_get_cpu(0);
    if (!cpu || (size && !data) || size > (uint64_t)UINT32_MAX + 1 - address) return false;
    uint8_t *destination = data;
    while (size) {
        hwaddr physical;
        size_t amount = MIN(size, TARGET_PAGE_SIZE - (address & ~TARGET_PAGE_MASK));
        if (virt_to_phys(address, &physical)) return false;
        MemoryRegionSection section = memory_region_find(get_system_memory(), physical, amount);
        bool ram = section.mr && memory_region_is_ram(section.mr) &&
                   int128_get64(section.size) >= amount;
        if (section.mr) memory_region_unref(section.mr);
        if (!ram || cpu_memory_rw_debug(cpu, address, destination, amount, false)) return false;
        address += amount; destination += amount; size -= amount;
    }
    return true;
}

static bool sun_profile_read(void *opaque, uint32_t address, void *data, size_t size)
{
    (void)opaque;
    return xemu_read_virtual_ram(address, data, size);
}

bool xemu_validate_doaxvb_sun_profile(void)
{
    return pgraph_sun_validate(&pgraph_sun_doaxvb_us, sun_profile_read, NULL) &&
           pgraph_sun_validate_draw_seams(sun_profile_read, NULL);
}

struct xbe *xemu_get_xbe_info(void)
{
    vaddr hdr_addr_virt = 0x10000;

    static struct xbe xbe = {0};

    if (xbe.headers) {
        free(xbe.headers);
        xbe.headers = NULL;
    }

    // Get physical page of headers
    hwaddr hdr_addr_phys = 0;
    if (virt_to_phys(hdr_addr_virt, &hdr_addr_phys) != 0) {
        return NULL;
    }

    // Check `XBEH` signature
    uint32_t sig = ldl_le_phys(&address_space_memory, hdr_addr_phys);
    if (sig != 0x48454258) {
        return NULL;
    }

    // Determine full length of headers
    xbe.headers_len = ldl_le_phys(&address_space_memory,
        hdr_addr_phys + offsetof(struct xbe_header, m_sizeof_headers));
    if (xbe.headers_len > 8*TARGET_PAGE_SIZE) {
        // Headers are unusually large
        return NULL;
    }

    xbe.headers = malloc(xbe.headers_len);
    assert(xbe.headers != NULL);

    // Read all XBE headers
    ssize_t bytes_read = virt_dma_memory_read(hdr_addr_virt,
                                              xbe.headers,
                                              xbe.headers_len);
    if (bytes_read != xbe.headers_len) {
        // Failed to read headers
        return NULL;
    }

    // Extract XBE header fields
    xbe.header = (struct xbe_header *)xbe.headers;

    // Get certificate
    vaddr cert_addr_virt = ldl_le_p(&xbe.header->m_certificate_addr);
    if ((cert_addr_virt == 0) || ((cert_addr_virt + sizeof(struct xbe_certificate)) > (hdr_addr_virt + xbe.headers_len))) {
        // Invalid certificate header (a valid certificate is expected for official titles)
        return NULL;
    }
    xbe.cert = (struct xbe_certificate *)(xbe.headers + cert_addr_virt - hdr_addr_virt);

    return &xbe;
}

/*
 * The running XBE's title id alone, with the checks xemu_get_xbe_info
 * makes, from four reads of the guest's memory: nothing allocated, no
 * headers copied (the core asks once a second).
 */
bool xemu_get_xbe_title_id(uint32_t *title_id)
{
    const vaddr hdr_addr_virt = 0x10000;
    hwaddr hdr_addr_phys = 0;
    uint32_t headers_len, titleid;
    vaddr cert_addr_virt;

    if (virt_to_phys(hdr_addr_virt, &hdr_addr_phys) != 0 ||
        ldl_le_phys(&address_space_memory, hdr_addr_phys) != 0x48454258) {
        return false;
    }
    headers_len = ldl_le_phys(&address_space_memory,
        hdr_addr_phys + offsetof(struct xbe_header, m_sizeof_headers));
    if (headers_len > 8 * TARGET_PAGE_SIZE) {
        return false;
    }
    cert_addr_virt = ldl_le_phys(&address_space_memory,
        hdr_addr_phys + offsetof(struct xbe_header, m_certificate_addr));
    if (cert_addr_virt == 0 ||
        cert_addr_virt + sizeof(struct xbe_certificate) >
            hdr_addr_virt + headers_len) {
        return false;
    }
    if (virt_dma_memory_read(cert_addr_virt +
                             offsetof(struct xbe_certificate, m_titleid),
                             &titleid, sizeof(titleid)) != sizeof(titleid)) {
        return false;
    }
    *title_id = le32_to_cpu(titleid);
    return true;
}

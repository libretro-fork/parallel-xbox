/*
 * QEMU Executable loader
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Gunzip functionality in this file is derived from u-boot:
 *
 * (C) Copyright 2008 Semihalf
 *
 * (C) Copyright 2000-2005
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/type-helpers.h"
#include "qemu/units.h"
#include "trace.h"
#include "hw/hw.h"
#include "disas/disas.h"
#include "migration/cpr.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "system/reset.h"
#include "system/system.h"
#include "uboot_image.h"
#include "hw/loader.h"
#include "system/memory.h"
#include "hw/boards.h"
#include "qemu/cutils.h"
#include "system/runstate.h"
#include "tcg/debuginfo.h"

#include <encodings/deflate.h>

static int roms_loaded;

/* return the size or -1 if error */
int64_t get_image_size(const char *filename, Error **errp)
{
    int fd;
    int64_t size;
    fd = qemu_open(filename, O_RDONLY | O_BINARY, errp);
    if (fd < 0)
        return -1;
    size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        error_setg_errno(errp, errno, "lseek failure: %s", filename);
        return -1;
    }
    close(fd);
    return size;
}

/* return the size or -1 if error */
ssize_t load_image_size(const char *filename, void *addr, size_t size)
{
    int fd;
    ssize_t actsize, l = 0;

    fd = qemu_open(filename, O_RDONLY | O_BINARY, NULL);
    if (fd < 0) {
        return -1;
    }

    while ((actsize = read(fd, addr + l, size - l)) > 0) {
        l += actsize;
    }

    close(fd);

    return actsize < 0 ? -1 : l;
}

#define HEAD_CRC    2
#define EXTRA_FIELD 4
#define ORIG_NAME   8
#define COMMENT     0x10
#define RESERVED    0xe0

#define DEFLATED    8


/* The PE/COFF MS-DOS stub magic number */
#define EFI_PE_MSDOS_MAGIC        "MZ"

/*
 * The Linux header magic number for a EFI PE/COFF
 * image targeting an unspecified architecture.
 */
#define EFI_PE_LINUX_MAGIC        "\xcd\x23\x82\x81"

/*
 * Bootable Linux kernel images may be packaged as EFI zboot images, which are
 * self-decompressing executables when loaded via EFI. The compressed payload
 * can also be extracted from the image and decompressed by a non-EFI loader.
 *
 * The de facto specification for this format is at the following URL:
 *
 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/firmware/efi/libstub/zboot-header.S
 *
 * This definition is based on Linux upstream commit 29636a5ce87beba.
 */
struct linux_efi_zboot_header {
    uint8_t     msdos_magic[2];         /* PE/COFF 'MZ' magic number */
    uint8_t     reserved0[2];
    uint8_t     zimg[4];                /* "zimg" for Linux EFI zboot images */
    uint32_t    payload_offset;         /* LE offset to compressed payload */
    uint32_t    payload_size;           /* LE size of the compressed payload */
    uint8_t     reserved1[8];
    char        compression_type[32];   /* Compression type, NUL terminated */
    uint8_t     linux_magic[4];         /* Linux header magic */
    uint32_t    pe_header_offset;       /* LE offset to the PE header */
};

/*
 * Functions for reboot-persistent memory regions.
 *  - used for vga bios and option roms.
 *  - also linux kernel (-kernel / -initrd).
 */

typedef struct Rom Rom;

struct Rom {
    char *name;
    char *path;

    /* datasize is the amount of memory allocated in "data". If datasize is less
     * than romsize, it means that the area from datasize to romsize is filled
     * with zeros.
     */
    size_t romsize;
    size_t datasize;

    uint8_t *data;
    MemoryRegion *mr;
    AddressSpace *as;
    int isrom;
    char *fw_dir;
    char *fw_file;
    GMappedFile *mapped_file;

    bool committed;

    hwaddr addr;
    QTAILQ_ENTRY(Rom) next;
};

static QTAILQ_HEAD(, Rom) roms = QTAILQ_HEAD_INITIALIZER(roms);

/*
 * rom->data can be heap-allocated or memory-mapped (e.g. when added with
 * rom_add_elf_program())
 */
static void rom_free_data(Rom *rom)
{
    if (rom->mapped_file) {
        g_mapped_file_unref(rom->mapped_file);
        rom->mapped_file = NULL;
    } else {
        g_free(rom->data);
    }

    rom->data = NULL;
}

static void rom_free(Rom *rom)
{
    rom_free_data(rom);
    g_free(rom->path);
    g_free(rom->name);
    g_free(rom->fw_dir);
    g_free(rom->fw_file);
    g_free(rom);
}

static inline bool rom_order_compare(Rom *rom, Rom *item)
{
    return ((uintptr_t)(void *)rom->as > (uintptr_t)(void *)item->as) ||
           (rom->as == item->as && rom->addr >= item->addr);
}

static void rom_insert(Rom *rom)
{
    Rom *item;

    if (roms_loaded) {
        hw_error ("ROM images must be loaded at startup\n");
    }

    /* The user didn't specify an address space, this is the default */
    if (!rom->as) {
        rom->as = &address_space_memory;
    }

    rom->committed = false;

    /* List is ordered by load address in the same address space */
    QTAILQ_FOREACH(item, &roms, next) {
        if (rom_order_compare(rom, item)) {
            continue;
        }
        QTAILQ_INSERT_BEFORE(item, rom, next);
        return;
    }
    QTAILQ_INSERT_TAIL(&roms, rom, next);
}

ssize_t rom_add_file(const char *file, const char *fw_dir,
                     hwaddr addr, int32_t bootindex,
                     bool has_option_rom, MemoryRegion *mr,
                     AddressSpace *as)
{
    Rom *rom;
    gsize size;
    g_autoptr(GError) gerr = NULL;
    char devpath[100];

    if (as && mr) {
        fprintf(stderr, "Specifying an Address Space and Memory Region is " \
                "not valid when loading a rom\n");
        /* We haven't allocated anything so we don't need any cleanup */
        return -1;
    }

    rom = g_malloc0(sizeof(*rom));
    rom->name = g_strdup(file);
    rom->path = qemu_find_file(QEMU_FILE_TYPE_BIOS, rom->name);
    rom->as = as;
    if (rom->path == NULL) {
        rom->path = g_strdup(file);
    }

    if (!g_file_get_contents(rom->path, (gchar **) &rom->data,
                             &size, &gerr)) {
        fprintf(stderr, "rom: file %-20s: error %s\n",
                rom->name, gerr->message);
        goto err;
    }

    if (fw_dir) {
        rom->fw_dir  = g_strdup(fw_dir);
        rom->fw_file = g_strdup(file);
    }
    rom->addr     = addr;
    rom->romsize  = size;
    rom->datasize = rom->romsize;
    rom_insert(rom);
    /* There is no fw_cfg device to hand the file to firmware */
    if (mr) {
        rom->mr = mr;
        snprintf(devpath, sizeof(devpath), "/rom@%s", file);
    } else {
        snprintf(devpath, sizeof(devpath), "/rom@" HWADDR_FMT_plx, addr);
    }

    add_boot_device_path(bootindex, NULL, devpath);
    return 0;

err:
    rom_free(rom);
    return -1;
}

void rom_add_blob(const char *name, const void *blob, size_t len,
                  size_t max_len, hwaddr addr, AddressSpace *as)
{
    Rom *rom;

    rom           = g_malloc0(sizeof(*rom));
    rom->name     = g_strdup(name);
    rom->as       = as;
    rom->addr     = addr;
    rom->romsize  = max_len ? max_len : len;
    rom->datasize = len;
    g_assert(rom->romsize >= rom->datasize);
    rom->data     = g_malloc0(rom->datasize);
    memcpy(rom->data, blob, len);
    rom_insert(rom);
}

ssize_t rom_add_vga(const char *file)
{
    return rom_add_file(file, "vgaroms", 0, -1, true, NULL, NULL);
}

ssize_t rom_add_option(const char *file, int32_t bootindex)
{
    return rom_add_file(file, "genroms", 0, bootindex, true, NULL, NULL);
}

static void rom_reset(void *unused)
{
    Rom *rom;

    QTAILQ_FOREACH(rom, &roms, next) {
        if (rom->fw_file) {
            continue;
        }
        /*
         * We don't need to fill in the RAM with ROM data because we'll fill
         * the data in during the next incoming migration in all cases.  Note
         * that some of those RAMs can actually be modified by the guest.
         */
        if (runstate_check(RUN_STATE_INMIGRATE)) {
            if (rom->data && rom->isrom) {
                /*
                 * Free it so that a rom_reset after migration doesn't
                 * overwrite a potentially modified 'rom'.
                 */
                rom_free_data(rom);
            }
            continue;
        }

        if (rom->data == NULL) {
            continue;
        }
        if (rom->mr) {
            void *host = memory_region_get_ram_ptr(rom->mr);
            memcpy(host, rom->data, rom->datasize);
            memset(host + rom->datasize, 0, rom->romsize - rom->datasize);
        } else {
            address_space_write_rom(rom->as, rom->addr, MEMTXATTRS_UNSPECIFIED,
                                    rom->data, rom->datasize);
            address_space_set(rom->as, rom->addr + rom->datasize, 0,
                              rom->romsize - rom->datasize,
                              MEMTXATTRS_UNSPECIFIED);
        }
        if (rom->isrom) {
            /* rom needs to be written only once */
            rom_free_data(rom);
        }
        /*
         * The rom loader is really on the same level as firmware in the guest
         * shadowing a ROM into RAM. Such a shadowing mechanism needs to ensure
         * that the instruction cache for that new region is clear, so that the
         * CPU definitely fetches its instructions from the just written data.
         */
        address_space_flush_icache_range(rom->as, rom->addr, rom->datasize);

        trace_loader_write_rom(rom->name, rom->addr, rom->datasize, rom->isrom);
    }
}

/* Return true if two consecutive ROMs in the ROM list overlap */
static bool roms_overlap(Rom *last_rom, Rom *this_rom)
{
    if (!last_rom) {
        return false;
    }
    return last_rom->as == this_rom->as &&
        last_rom->addr + last_rom->romsize > this_rom->addr;
}

static const char *rom_as_name(Rom *rom)
{
    const char *name = rom->as ? rom->as->name : NULL;
    return name ?: "anonymous";
}

static void rom_print_overlap_error_header(void)
{
    error_report("Some ROM regions are overlapping");
    error_printf(
        "These ROM regions might have been loaded by "
        "direct user request or by default.\n"
        "They could be BIOS/firmware images, a guest kernel, "
        "initrd or some other file loaded into guest memory.\n"
        "Check whether you intended to load all this guest code, and "
        "whether it has been built to load to the correct addresses.\n");
}

static void rom_print_one_overlap_error(Rom *last_rom, Rom *rom)
{
    error_printf(
        "\nThe following two regions overlap (in the %s address space):\n",
        rom_as_name(rom));
    error_printf(
        "  %s (addresses 0x" HWADDR_FMT_plx " - 0x" HWADDR_FMT_plx ")\n",
        last_rom->name, last_rom->addr, last_rom->addr + last_rom->romsize);
    error_printf(
        "  %s (addresses 0x" HWADDR_FMT_plx " - 0x" HWADDR_FMT_plx ")\n",
        rom->name, rom->addr, rom->addr + rom->romsize);
}

int rom_check_and_register_reset(void)
{
    MemoryRegionSection section;
    Rom *rom, *last_rom = NULL;
    bool found_overlap = false;

    QTAILQ_FOREACH(rom, &roms, next) {
        if (rom->fw_file) {
            continue;
        }
        if (!rom->mr) {
            if (roms_overlap(last_rom, rom)) {
                if (!found_overlap) {
                    found_overlap = true;
                    rom_print_overlap_error_header();
                }
                rom_print_one_overlap_error(last_rom, rom);
                /* Keep going through the list so we report all overlaps */
            }
            last_rom = rom;
        }
        section = memory_region_find(rom->mr ? rom->mr : get_system_memory(),
                                     rom->addr, 1);
        rom->isrom = int128_nz(section.size) && memory_region_is_rom(section.mr);
        memory_region_unref(section.mr);
    }
    if (found_overlap) {
        return -1;
    }

    qemu_register_reset(rom_reset, NULL);
    roms_loaded = 1;
    return 0;
}

static Rom *find_rom(hwaddr addr, size_t size)
{
    Rom *rom;

    QTAILQ_FOREACH(rom, &roms, next) {
        if (rom->fw_file) {
            continue;
        }
        if (rom->mr) {
            continue;
        }
        if (rom->addr > addr) {
            continue;
        }
        if (rom->addr + rom->romsize < addr + size) {
            continue;
        }
        return rom;
    }
    return NULL;
}

typedef struct RomSec {
    hwaddr base;
    int se; /* start/end flag */
} RomSec;


void *rom_ptr(hwaddr addr, size_t size)
{
    Rom *rom;

    rom = find_rom(addr, size);
    if (!rom || !rom->data)
        return NULL;
    return rom->data + (addr - rom->addr);
}

typedef struct FindRomCBData {
    size_t size; /* Amount of data we want from ROM, in bytes */
    MemoryRegion *mr; /* MR at the unaliased guest addr */
    hwaddr xlat; /* Offset of addr within mr */
    void *rom; /* Output: rom data pointer, if found */
} FindRomCBData;

typedef enum HexRecord HexRecord;
enum HexRecord {
    DATA_RECORD = 0,
    EOF_RECORD,
    EXT_SEG_ADDR_RECORD,
    START_SEG_ADDR_RECORD,
    EXT_LINEAR_ADDR_RECORD,
    START_LINEAR_ADDR_RECORD,
};

/* Each record contains a 16-bit address which is combined with the upper 16
 * bits of the implicit "next address" to form a 32-bit address.
 */
#define NEXT_ADDR_MASK 0xffff0000

#define DATA_FIELD_MAX_LEN 0xff
#define LEN_EXCEPT_DATA 0x5
/* 0x5 = sizeof(byte_count) + sizeof(address) + sizeof(record_type) +
 *       sizeof(checksum) */
typedef struct {
    uint8_t byte_count;
    uint16_t address;
    uint8_t record_type;
    uint8_t data[DATA_FIELD_MAX_LEN];
    uint8_t checksum;
} HexLine;

typedef struct {
    const char *filename;
    HexLine line;
    uint8_t *bin_buf;
    hwaddr *start_addr;
    int total_size;
    uint32_t next_address_to_write;
    uint32_t current_address;
    uint32_t current_rom_index;
    uint32_t rom_start_address;
    AddressSpace *as;
    bool complete;
} HexParser;


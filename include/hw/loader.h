#ifndef LOADER_H
#define LOADER_H
#include "hw/nvram/fw_cfg.h"
#include "qemu/typedefs.h"

/* loader.c */
/**
 * get_image_size: retrieve size of an image file
 * @filename: Path to the image file
 *
 * Returns the size of the image file on success, -1 otherwise.
 * On error, errno is also set as appropriate.
 */
int64_t get_image_size(const char *filename, Error **errp);
/**
 * load_image_size: load an image file into specified buffer
 * @filename: Path to the image file
 * @addr: Buffer to load image into
 * @size: Size of buffer in bytes
 *
 * Load an image file from disk into the specified buffer.
 * If the image is larger than the specified buffer, only
 * @size bytes are read (this is not considered an error).
 *
 * Prefer to use the GLib function g_file_get_contents() rather
 * than a "get_image_size()/g_malloc()/load_image_size()" sequence.
 *
 * Returns the number of bytes read, or -1 on error. On error,
 * errno is also set as appropriate.
 */
ssize_t load_image_size(const char *filename, void *addr, size_t size);

/* This is the limit on the maximum uncompressed image size that
 * load_image_gzipped_buffer() will read. It prevents
 * g_malloc() in those functions from allocating a huge amount of memory.
 */
#define LOAD_IMAGE_MAX_GUNZIP_BYTES (256 << 20)

#define ELF_LOAD_FAILED       -1
#define ELF_LOAD_NOT_ELF      -2
#define ELF_LOAD_WRONG_ARCH   -3
#define ELF_LOAD_WRONG_ENDIAN -4
#define ELF_LOAD_TOO_BIG      -5

/** load_elf_ram_sym:
 * @filename: Path of ELF file
 * @elf_note_fn: optional function to parse ELF Note type
 *               passed via @translate_opaque
 * @translate_fn: optional function to translate load addresses
 * @translate_opaque: opaque data passed to @translate_fn
 * @pentry: Populated with program entry point. Ignored if NULL.
 * @lowaddr: Populated with lowest loaded address. Ignored if NULL.
 * @highaddr: Populated with highest loaded address. Ignored if NULL.
 * @pflags: Populated with ELF processor-specific flags. Ignore if NULL.
 * @elf_data_order: Expected ELF endianness (ELFDATA2LSB or ELFDATA2MSB).
 * @elf_machine: Expected ELF machine type
 * @clear_lsb: Set to mask off LSB of addresses (Some architectures use
 *             this for non-address data)
 * @data_swab: Set to order of byte swapping for data. 0 for no swap, 1
 *             for swapping bytes within halfwords, 2 for bytes within
 *             words and 3 for within doublewords.
 * @as: The AddressSpace to load the ELF to. The value of address_space_memory
 *      is used if nothing is supplied here.
 * @load_rom : Load ELF binary as ROM
 * @sym_cb: Callback function for symbol table entries
 *
 * Load an ELF file's contents to the emulated system's address space.
 * Clients may optionally specify a callback to perform address
 * translations. @pentry, @lowaddr and @highaddr are optional pointers
 * which will be populated with various load information. @bigendian and
 * @elf_machine give the expected endianness and machine for the ELF the
 * load will fail if the target ELF does not match. Some architectures
 * have some architecture-specific behaviours that come into effect when
 * their particular values for @elf_machine are set.
 * If @elf_machine is EM_NONE then the machine type will be read from the
 * ELF header and no checks will be carried out against the machine type.
 */
typedef void (*symbol_fn_t)(const char *st_name, int st_info,
                            uint64_t st_value, uint64_t st_size);

#define LOAD_UIMAGE_LOADADDR_INVALID (-1)

ssize_t rom_add_file(const char *file, const char *fw_dir,
                     hwaddr addr, int32_t bootindex,
                     bool has_option_rom, MemoryRegion *mr, AddressSpace *as);
MemoryRegion *rom_add_blob(const char *name, const void *blob, size_t len,
                           size_t max_len, hwaddr addr,
                           const char *fw_file_name,
                           FWCfgCallback fw_callback,
                           void *callback_opaque, AddressSpace *as,
                           bool read_only);
int rom_add_elf_program(const char *name, GMappedFile *mapped_file, void *data,
                        size_t datasize, size_t romsize, hwaddr addr,
                        AddressSpace *as);
int rom_check_and_register_reset(void);

void *rom_ptr(hwaddr addr, size_t size);
/**
 * rom_ptr_for_as: Return a pointer to ROM blob data for the address
 * @as: AddressSpace to look for the ROM blob in
 * @addr: Address within @as
 * @size: size of data required in bytes
 *
 * Returns: pointer into the data which backs the matching ROM blob,
 * or NULL if no blob covers the address range.
 *
 * This function looks for a ROM blob which covers the specified range
 * of bytes of length @size starting at @addr within the address space
 * @as. This is useful for code which runs as part of board
 * initialization or CPU reset which wants to read data that is part
 * of a user-supplied guest image or other guest memory contents, but
 * which runs before the ROM loader's reset function has copied the
 * blobs into guest memory.
 *
 * rom_ptr_for_as() will look not just for blobs loaded directly to
 * the specified address, but also for blobs which were loaded to an
 * alias of the region at a different location in the AddressSpace.
 * In other words, if a machine model has RAM at address 0x0000_0000
 * which is aliased to also appear at 0x1000_0000, rom_ptr_for_as()
 * will return the correct data whether the guest image was linked and
 * loaded at 0x0000_0000 or 0x1000_0000.  Contrast rom_ptr(), which
 * will only return data if the image load address is an exact match
 * with the queried address.
 *
 * New code should prefer to use rom_ptr_for_as() instead of
 * rom_ptr().
 */

#define rom_add_file_fixed(_f, _a, _i)          \
    rom_add_file(_f, NULL, _a, _i, false, NULL, NULL)
#define rom_add_blob_fixed(_f, _b, _l, _a)      \
    rom_add_blob(_f, _b, _l, _l, _a, NULL, NULL, NULL, NULL, true)
#define rom_add_file_mr(_f, _mr, _i)            \
    rom_add_file(_f, NULL, 0, _i, false, _mr, NULL)
#define rom_add_file_as(_f, _as, _i)            \
    rom_add_file(_f, NULL, 0, _i, false, NULL, _as)
#define rom_add_file_fixed_as(_f, _a, _i, _as)          \
    rom_add_file(_f, NULL, _a, _i, false, NULL, _as)
#define rom_add_blob_fixed_as(_f, _b, _l, _a, _as)      \
    rom_add_blob(_f, _b, _l, _l, _a, NULL, NULL, NULL, _as, true)

ssize_t rom_add_vga(const char *file);
ssize_t rom_add_option(const char *file, int32_t bootindex);

/* This is the usual maximum in uboot, so if a uImage overflows this, it would
 * overflow on real hardware too. */
#define UBOOT_MAX_GUNZIP_BYTES (64 << 20)

typedef struct RomGap {
    hwaddr base;
    size_t size;
} RomGap;

#endif

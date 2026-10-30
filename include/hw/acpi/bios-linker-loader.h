#ifndef BIOS_LINKER_LOADER_H
#define BIOS_LINKER_LOADER_H


typedef struct BIOSLinker {
    GArray *cmd_blob;
    GArray *file_list;
} BIOSLinker;

void bios_linker_loader_add_checksum(BIOSLinker *linker, const char *file,
                                     unsigned start_offset, unsigned size,
                                     unsigned checksum_offset);

#endif

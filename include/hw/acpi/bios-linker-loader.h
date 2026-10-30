#ifndef BIOS_LINKER_LOADER_H
#define BIOS_LINKER_LOADER_H


typedef struct BIOSLinker {
    GArray *cmd_blob;
    GArray *file_list;
} BIOSLinker;

#endif

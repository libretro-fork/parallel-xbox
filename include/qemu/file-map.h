/*
 * A whole file to read: its memory mapping where the platform has one
 * (libretro-common's filestream, POSIX mmap or a Win32 file mapping), the
 * page cache read in place and nothing copied; a buffer read in one call
 * where it has none.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_FILE_MAP_H
#define QEMU_FILE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct XemuFileMap {
    const uint8_t *data;
    size_t len;
    void *file;  /* the stream the mapping belongs to, NULL for a buffer */
    void *owned; /* the buffer, where there is no mapping */
} XemuFileMap;

/* Opens path read-only and makes all of it readable at m->data. An empty
 * file maps as data NULL, len 0. */
bool xemu_file_map(XemuFileMap *m, const char *path);
void xemu_file_unmap(XemuFileMap *m);

/* Whether m->data is the file's mapping (the page cache) rather than a
 * buffer of its own: a caller that keeps a large file open for a session
 * keeps only a mapping. */
static inline bool xemu_file_map_is_mapping(const XemuFileMap *m)
{
    return m->file != NULL;
}

#endif /* QEMU_FILE_MAP_H */

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_GL_SHADER_BINARY_H
#define NV2A_GL_SHADER_BINARY_H

#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include "qemu/file-map.h"

/* Disk results and live bindings transfer this owner with the payload view.
 * Bound retained file handles across both the queue and the cache. */
#define SHADER_BINARY_MAX_MAPPINGS 64
typedef struct ShaderBinaryOwner {
    XemuFileMap file;
    retro_atomic_int_t *mapping_count;
} ShaderBinaryOwner;

static inline void *shader_binary_take_file(ShaderBinaryOwner *owner,
                                            XemuFileMap *file,
                                            const void *data, size_t size,
                                            retro_atomic_int_t *mapping_count)
{
    *owner = (ShaderBinaryOwner){0};
    if (file->file && retro_atomic_fetch_add_int(mapping_count, 1) >=
                      SHADER_BINARY_MAX_MAPPINGS) {
        retro_atomic_fetch_sub_int(mapping_count, 1);
        void *copy = malloc(size);
        if (!copy) {
            abort();
        }
        memcpy(copy, data, size);
        xemu_file_unmap(file);
        return copy;
    }
    owner->file = *file;
    if (file->file) {
        owner->mapping_count = mapping_count;
    }
    *file = (XemuFileMap){0};
    return (void *)data;
}

static inline void shader_binary_release(void *data, ShaderBinaryOwner *owner)
{
    if (owner->file.data) {
        xemu_file_unmap(&owner->file);
        if (owner->mapping_count) {
            retro_atomic_fetch_sub_int(owner->mapping_count, 1);
        }
    } else {
        free(data);
    }
    *owner = (ShaderBinaryOwner){0};
}

#endif

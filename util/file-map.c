/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/file-map.h"
#include <streams/file_stream.h>

bool xemu_file_map(XemuFileMap *m, const char *path)
{
    RFILE *f;
    int64_t len;
    const uint8_t *p;

    m->data = NULL;
    m->len = 0;
    m->file = NULL;
    m->owned = NULL;
    if (!path || !path[0]) {
        return false;
    }
    f = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
                        RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS);
    if (!f) {
        return false;
    }
    p = filestream_get_mapped_ptr(f, &len);
    if (p) {
        m->data = p;
        m->len = (size_t)len;
        m->file = f;
        return true;
    }
    len = filestream_get_size(f);
    if (len < 0 || (uint64_t)len > SIZE_MAX) {
        filestream_close(f);
        return false;
    }
    if (len == 0) {
        filestream_close(f);
        return true;
    }
    m->owned = g_malloc(len);
    if (filestream_read(f, m->owned, len) != len) {
        g_free(m->owned);
        m->owned = NULL;
        filestream_close(f);
        return false;
    }
    filestream_close(f);
    m->data = m->owned;
    m->len = (size_t)len;
    return true;
}

void xemu_file_unmap(XemuFileMap *m)
{
    if (m->file) {
        filestream_close(m->file);
    }
    g_free(m->owned);
    m->data = NULL;
    m->len = 0;
    m->file = NULL;
    m->owned = NULL;
}

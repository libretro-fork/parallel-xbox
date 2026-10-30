/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QemuDmaBuf struct and helpers used for accessing its data
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "ui/dmabuf.h"

struct QemuDmaBuf {
    int       fd[DMABUF_MAX_PLANES];
    uint32_t  width;
    uint32_t  height;
    uint32_t  offset[DMABUF_MAX_PLANES];
    uint32_t  stride[DMABUF_MAX_PLANES];
    uint32_t  num_planes;
    uint32_t  fourcc;
    uint64_t  modifier;
    uint32_t  texture;
    uint32_t  x;
    uint32_t  y;
    uint32_t  backing_width;
    uint32_t  backing_height;
    bool      y0_top;
    void      *sync;
    int       fence_fd;
    bool      allow_fences;
    bool      draw_submitted;
};

void qemu_dmabuf_free(QemuDmaBuf *dmabuf)
{
    if (dmabuf == NULL) {
        return;
    }

    g_free(dmabuf);
}

uint32_t qemu_dmabuf_get_width(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->width;
}

uint32_t qemu_dmabuf_get_height(QemuDmaBuf *dmabuf)
{
    assert(dmabuf != NULL);

    return dmabuf->height;
}


/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QemuDmaBuf struct and helpers used for accessing its data
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef DMABUF_H
#define DMABUF_H

#define DMABUF_MAX_PLANES 4

typedef struct QemuDmaBuf QemuDmaBuf;

void qemu_dmabuf_free(QemuDmaBuf *dmabuf);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QemuDmaBuf, qemu_dmabuf_free);

uint32_t qemu_dmabuf_get_width(QemuDmaBuf *dmabuf);
uint32_t qemu_dmabuf_get_height(QemuDmaBuf *dmabuf);

#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIBRETRO_VFS_NEGOTIATE_H
#define LIBRETRO_VFS_NEGOTIATE_H
#include <libretro.h>

/* Never inspect the v6 tail of an interface returned for an older request. */
static inline bool lr_vfs_negotiate(retro_environment_t environment_cb,
                                    struct retro_vfs_interface_info *info)
{
    info->required_interface_version = 6;
    info->iface = NULL;
    if (environment_cb && environment_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, info) &&
        info->iface) {
        return true;
    }
    info->required_interface_version = 3;
    info->iface = NULL;
    if (environment_cb && environment_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, info) &&
        info->iface) {
        return true;
    }
    info->iface = NULL;
    return false;
}

static inline retro_vfs_read_at_t lr_vfs_positional_read(
    const struct retro_vfs_interface_info *info)
{
    return info->required_interface_version >= 6 && info->iface ?
        info->iface->read_at : NULL;
}
#endif

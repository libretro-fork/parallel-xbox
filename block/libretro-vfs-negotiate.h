/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIBRETRO_VFS_NEGOTIATE_H
#define LIBRETRO_VFS_NEGOTIATE_H
#include <libretro.h>

/* Never inspect the v6 tail of an interface returned for an older request.
 * v7 first: under it filestream_get_mapped_ptr() lends the frontend's
 * mapping of a file opened to be mapped (disc images, firmware), where an
 * older frontend's files are read and copied. */
static inline bool lr_vfs_negotiate(retro_environment_t environment_cb,
                                    struct retro_vfs_interface_info *info)
{
    static const unsigned versions[] = { 7, 6 };
    unsigned i;
    for (i = 0; i < sizeof(versions) / sizeof(versions[0]); i++) {
        info->required_interface_version = versions[i];
        info->iface = NULL;
        if (environment_cb &&
            environment_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, info) &&
            info->iface) {
            return true;
        }
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

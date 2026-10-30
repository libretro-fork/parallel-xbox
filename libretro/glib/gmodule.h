/*
 * The in-tree GLib: <gmodule.h>. QEMU includes it for its loadable
 * modules, which the core is built without (no CONFIG_MODULES): the types
 * only, nothing is ever loaded.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef LR_GLIB_GMODULE_H
#define LR_GLIB_GMODULE_H

#include <glib.h>

typedef struct _GModule GModule;
typedef enum {
    G_MODULE_BIND_LAZY = 1 << 0,
    G_MODULE_BIND_LOCAL = 1 << 1,
    G_MODULE_BIND_MASK = 0x03
} GModuleFlags;

#endif

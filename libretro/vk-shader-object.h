/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef XEMU_VK_SHADER_OBJECT_H
#define XEMU_VK_SHADER_OBJECT_H
#include <stdbool.h>

/* Include after Vulkan headers. Query the device's resolver rather than an
 * instance resolver which may return commands the device did not enable. */
static inline bool xemu_vk_shader_object_procs_ready(
    VkDevice device, PFN_vkGetDeviceProcAddr resolve,
    const VkPhysicalDeviceFeatures *enabled)
{
    if (!device || !resolve || !enabled) return false;
#define XEMU_VK_SO_PROC(name) if (!resolve(device, #name)) return false;
#define XEMU_VK_SO_FEATURE_PROC(feature, name) \
    if (enabled->feature && !resolve(device, #name)) return false;
#include "vk-shader-object-procs.inc"
#undef XEMU_VK_SO_PROC
#undef XEMU_VK_SO_FEATURE_PROC
    return true;
}
#endif

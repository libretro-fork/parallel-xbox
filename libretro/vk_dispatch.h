/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef XEMU_VK_DISPATCH_H
#define XEMU_VK_DISPATCH_H

/* No Vulkan loader linkage or OS library discovery in the core. */
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifdef _WIN32
#include <windows.h>
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#endif
#include <vulkan/vulkan.h>

extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#define XEMU_VK_GLOBAL(name) extern PFN_##name name;
#define XEMU_VK_GLOBAL_OPTIONAL(name) extern PFN_##name name;
#define XEMU_VK_INSTANCE(name) extern PFN_##name name;
#define XEMU_VK_INSTANCE_OPTIONAL(name) extern PFN_##name name;
#define XEMU_VK_DEVICE(name) extern PFN_##name name;
#define XEMU_VK_DEVICE_OPTIONAL(name) extern PFN_##name name;
#include "vk_dispatch.inc"
#undef XEMU_VK_GLOBAL
#undef XEMU_VK_GLOBAL_OPTIONAL
#undef XEMU_VK_INSTANCE
#undef XEMU_VK_INSTANCE_OPTIONAL
#undef XEMU_VK_DEVICE
#undef XEMU_VK_DEVICE_OPTIONAL

/* Return the first missing required entry point, or NULL. Initialization is
 * owner-only, before renderer workers start. Optional entries may be NULL. */
const char *xemu_vk_load_global(PFN_vkGetInstanceProcAddr resolver);
const char *xemu_vk_load_instance(PFN_vkGetInstanceProcAddr resolver, VkInstance instance);
const char *xemu_vk_load_device(VkDevice device);
const char *xemu_vk_load_dispatch(PFN_vkGetInstanceProcAddr resolver,
                                  VkInstance instance, VkDevice device);
void xemu_vk_clear_dispatch(void);
#endif

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "vk_dispatch.h"
#include <stddef.h>

PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#define XEMU_VK_GLOBAL(name) PFN_##name name;
#define XEMU_VK_GLOBAL_OPTIONAL(name) PFN_##name name;
#define XEMU_VK_INSTANCE(name) PFN_##name name;
#define XEMU_VK_INSTANCE_OPTIONAL(name) PFN_##name name;
#define XEMU_VK_DEVICE(name) PFN_##name name;
#define XEMU_VK_DEVICE_OPTIONAL(name) PFN_##name name;
#include "vk_dispatch.inc"
#undef XEMU_VK_GLOBAL
#undef XEMU_VK_GLOBAL_OPTIONAL
#undef XEMU_VK_INSTANCE
#undef XEMU_VK_INSTANCE_OPTIONAL
#undef XEMU_VK_DEVICE
#undef XEMU_VK_DEVICE_OPTIONAL

const char *xemu_vk_load_global(PFN_vkGetInstanceProcAddr resolver)
{
    if (!resolver) return "vkGetInstanceProcAddr";
    vkGetInstanceProcAddr = resolver;
    const char *missing = NULL;
#define XEMU_VK_GLOBAL(name) name = (PFN_##name)resolver(VK_NULL_HANDLE, #name); if (!name && !missing) missing = #name;
#define XEMU_VK_GLOBAL_OPTIONAL(name) name = (PFN_##name)resolver(VK_NULL_HANDLE, #name);
#define XEMU_VK_INSTANCE(name) 
#define XEMU_VK_INSTANCE_OPTIONAL(name) 
#define XEMU_VK_DEVICE(name) 
#define XEMU_VK_DEVICE_OPTIONAL(name) 
#include "vk_dispatch.inc"
#undef XEMU_VK_GLOBAL
#undef XEMU_VK_GLOBAL_OPTIONAL
#undef XEMU_VK_INSTANCE
#undef XEMU_VK_INSTANCE_OPTIONAL
#undef XEMU_VK_DEVICE
#undef XEMU_VK_DEVICE_OPTIONAL
    return missing;
}

const char *xemu_vk_load_instance(PFN_vkGetInstanceProcAddr resolver, VkInstance instance)
{
    if (!resolver) return "vkGetInstanceProcAddr";
    vkGetInstanceProcAddr = resolver;
    const char *missing = NULL;
#define XEMU_VK_GLOBAL(name) 
#define XEMU_VK_GLOBAL_OPTIONAL(name) 
#define XEMU_VK_INSTANCE(name) name = (PFN_##name)resolver(instance, #name); if (!name && !missing) missing = #name;
#define XEMU_VK_INSTANCE_OPTIONAL(name) name = (PFN_##name)resolver(instance, #name);
#define XEMU_VK_DEVICE(name) name = (PFN_##name)resolver(instance, #name);
#define XEMU_VK_DEVICE_OPTIONAL(name) name = (PFN_##name)resolver(instance, #name);
#include "vk_dispatch.inc"
#undef XEMU_VK_GLOBAL
#undef XEMU_VK_GLOBAL_OPTIONAL
#undef XEMU_VK_INSTANCE
#undef XEMU_VK_INSTANCE_OPTIONAL
#undef XEMU_VK_DEVICE
#undef XEMU_VK_DEVICE_OPTIONAL
    return missing;
}

const char *xemu_vk_load_device(VkDevice device)
{
    if (!vkGetDeviceProcAddr) return "vkGetDeviceProcAddr";
    const char *missing = NULL;
#define XEMU_VK_GLOBAL(name) 
#define XEMU_VK_GLOBAL_OPTIONAL(name) 
#define XEMU_VK_INSTANCE(name) 
#define XEMU_VK_INSTANCE_OPTIONAL(name) 
#define XEMU_VK_DEVICE(name) name = (PFN_##name)vkGetDeviceProcAddr(device, #name); if (!name && !missing) missing = #name;
#define XEMU_VK_DEVICE_OPTIONAL(name) name = (PFN_##name)vkGetDeviceProcAddr(device, #name);
#include "vk_dispatch.inc"
#undef XEMU_VK_GLOBAL
#undef XEMU_VK_GLOBAL_OPTIONAL
#undef XEMU_VK_INSTANCE
#undef XEMU_VK_INSTANCE_OPTIONAL
#undef XEMU_VK_DEVICE
#undef XEMU_VK_DEVICE_OPTIONAL
    return missing;
}

void xemu_vk_clear_dispatch(void)
{
    vkGetInstanceProcAddr = NULL;
#define XEMU_VK_GLOBAL(name) name = NULL;
#define XEMU_VK_GLOBAL_OPTIONAL(name) name = NULL;
#define XEMU_VK_INSTANCE(name) name = NULL;
#define XEMU_VK_INSTANCE_OPTIONAL(name) name = NULL;
#define XEMU_VK_DEVICE(name) name = NULL;
#define XEMU_VK_DEVICE_OPTIONAL(name) name = NULL;
#include "vk_dispatch.inc"
#undef XEMU_VK_GLOBAL
#undef XEMU_VK_GLOBAL_OPTIONAL
#undef XEMU_VK_INSTANCE
#undef XEMU_VK_INSTANCE_OPTIONAL
#undef XEMU_VK_DEVICE
#undef XEMU_VK_DEVICE_OPTIONAL
}

const char *xemu_vk_load_dispatch(PFN_vkGetInstanceProcAddr resolver,
                                  VkInstance instance, VkDevice device)
{
    if (!instance || !device) return "negotiated Vulkan instance/device";
    const char *missing = xemu_vk_load_instance(resolver, instance);
    return missing ? missing : xemu_vk_load_device(device);
}

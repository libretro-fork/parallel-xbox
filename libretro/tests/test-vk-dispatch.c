/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "vk_dispatch.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static VkInstance expected_instance;
static VkDevice expected_device;
static const char *missing_name;
static unsigned generation, instance_calls, device_calls, global_calls;
static void VKAPI_CALL target_a(void) {}
static void VKAPI_CALL target_b(void) {}

static PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice device, const char *name)
{
    assert(device == expected_device);
    device_calls++;
    if ((missing_name && !strcmp(name, missing_name)) || strstr(name, "EXT") ||
        strstr(name, "KHR")) return NULL;
    return generation ? target_b : target_a;
}

static PFN_vkVoidFunction VKAPI_CALL instance_proc(VkInstance instance, const char *name)
{
    if (instance) {
        assert(instance == expected_instance);
        instance_calls++;
    } else {
        global_calls++;
    }
    if (missing_name && !strcmp(name, missing_name)) return NULL;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)device_proc;
    if (strstr(name, "EXT") || strstr(name, "KHR")) return NULL;
    /* Device commands queried via GIPA are deliberately different trampolines. */
    return target_a;
}

int main(void)
{
    expected_instance = (VkInstance)(uintptr_t)1;
    expected_device = (VkDevice)(uintptr_t)2;
    assert(xemu_vk_load_dispatch(NULL, expected_instance, expected_device));
    assert(xemu_vk_load_dispatch(instance_proc, VK_NULL_HANDLE, expected_device));
    assert(!instance_calls && !device_calls && !global_calls);
    assert(!xemu_vk_load_dispatch(instance_proc, expected_instance, expected_device));
    assert(instance_calls && device_calls && !global_calls);
    assert((PFN_vkVoidFunction)vkCreateImage == target_a);
    assert(!vkCmdSetCullModeEXT); /* Optional functions may be absent. */

    generation = 1;
    expected_instance = (VkInstance)(uintptr_t)3;
    expected_device = (VkDevice)(uintptr_t)4;
    assert(!xemu_vk_load_dispatch(instance_proc, expected_instance, expected_device));
    assert((PFN_vkVoidFunction)vkCreateImage == target_b); /* Fresh GDPA, not GIPA. */
    missing_name = "vkCreateImage";
    assert(!strcmp(xemu_vk_load_dispatch(instance_proc, expected_instance, expected_device),
                   missing_name));
    assert(!vkCreateImage); /* Never retain an old device's pointer on failure. */
    missing_name = "vkGetDeviceProcAddr";
    unsigned calls = device_calls;
    assert(!strcmp(xemu_vk_load_dispatch(instance_proc, expected_instance, expected_device),
                   missing_name));
    assert(device_calls == calls);
    xemu_vk_clear_dispatch();
    assert(!vkGetInstanceProcAddr);
#define XEMU_VK_GLOBAL(name) assert(!name);
#define XEMU_VK_GLOBAL_OPTIONAL(name) assert(!name);
#define XEMU_VK_INSTANCE(name) assert(!name);
#define XEMU_VK_INSTANCE_OPTIONAL(name) assert(!name);
#define XEMU_VK_DEVICE(name) assert(!name);
#define XEMU_VK_DEVICE_OPTIONAL(name) assert(!name);
#include "vk_dispatch.inc"
    puts("PASS: frontend-only resolution, direct device dispatch, optional misses, required failures and reload/clear ownership");
    return 0;
}

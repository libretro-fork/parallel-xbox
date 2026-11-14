/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* OS loader discovery belongs only to standalone GPU regression probes. */
#include "vk-test-loader.h"
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif

static void *library;

VkResult test_vk_initialize(void)
{
    if (library) return VK_SUCCESS;
#ifdef _WIN32
    library = (void *)LoadLibraryA("vulkan-1.dll");
    PFN_vkGetInstanceProcAddr resolver = library ?
        (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress((HMODULE)library,
                                                 "vkGetInstanceProcAddr") : NULL;
#else
    library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
#ifdef __APPLE__
    if (!library) library = dlopen("libvulkan.1.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!library) library = dlopen("libMoltenVK.dylib", RTLD_NOW | RTLD_LOCAL);
#endif
    PFN_vkGetInstanceProcAddr resolver = library ?
        (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr") : NULL;
#endif
    if (!resolver || xemu_vk_load_global(resolver)) {
        test_vk_finalize();
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

static void require_dispatch(const char *missing)
{
    if (missing) {
        fprintf(stderr, "Vulkan test entry point missing: %s\n", missing);
        abort();
    }
}

void test_vk_load_instance(VkInstance instance)
{
    require_dispatch(xemu_vk_load_instance(vkGetInstanceProcAddr, instance));
}

void test_vk_load_device(VkDevice device)
{
    require_dispatch(xemu_vk_load_device(device));
}

void test_vk_finalize(void)
{
    xemu_vk_clear_dispatch();
    if (library) {
#ifdef _WIN32
        FreeLibrary((HMODULE)library);
#else
        dlclose(library);
#endif
        library = NULL;
    }
}

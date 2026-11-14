/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Test-only bootstrap: production resolves through the libretro frontend. */
#ifndef XEMU_VK_TEST_LOADER_H
#define XEMU_VK_TEST_LOADER_H
#include "vk_dispatch.h"
VkResult test_vk_initialize(void);
void test_vk_load_instance(VkInstance instance);
void test_vk_load_device(VkDevice device);
void test_vk_finalize(void);
#endif

/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
 * Copyright (c) 2026 Réda Chérif-Touil
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "ui/xemu-settings.h"
#include "renderer.h"
#include "xemu-version.h"
#include "libretro_shared.h"

/* Core 1.2 properties need no device feature enable. Conservative fallback on
 * older APIs, even if a driver exposes an optional extension. Published once
 * per device before shader workers start; queried with stack-only storage. */
static void init_float_controls(PGRAPHVkState *r)
{
    pgraph_vk_preserve_float32 = false;
    if (r->vk_api_version < VK_API_VERSION_1_2) {
        return;
    }
    VkPhysicalDeviceFloatControlsProperties controls = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES,
    };
    VkPhysicalDeviceProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &controls,
    };
    vkGetPhysicalDeviceProperties2(r->physical_device, &properties);
    pgraph_vk_preserve_float32 = controls.shaderSignedZeroInfNanPreserveFloat32;
}

uint32_t pgraph_vk_get_memory_type(PGRAPHState *pg, uint32_t type_bits,
                                   VkMemoryPropertyFlags properties)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkPhysicalDeviceMemoryProperties prop;
    vkGetPhysicalDeviceMemoryProperties(r->physical_device, &prop);
    for (uint32_t i = 0; i < prop.memoryTypeCount; i++) {
        if ((prop.memoryTypes[i].propertyFlags & properties) == properties &&
            type_bits & (1 << i)) {
            return i;
        }
    }
    return 0xFFFFFFFF; // Unable to find memoryType
}

static void reclaim_retired_images(void *opaque)
{
    pgraph_vk_reclaim_retired_images(opaque);
}

static bool init_allocator(PGRAPHState *pg, Error **errp)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkResult result;

    result = xemu_vk_allocator_create(r->physical_device, r->device,
        r->memory_budget_extension_enabled, &r->allocator);
    if (result != VK_SUCCESS) {
        error_setg(errp, "Xbox Vulkan memory heap initialization failed: %d", result);
        return false;
    }

    r->reserved_surface_scale = MAX(g_config.display.quality.surface_scale, 1);
    xemu_vk_allocator_set_reclaim(r->allocator, reclaim_retired_images, r);
    return true;
}

/*
 * The instance and device the core made with the frontend (libretro_shared.h)
 * in place of ones of our own: resolve only from the frontend's loader, and
 * the queue, features and extensions are the ones the device was made with.
 */
static bool adopt_frontend_device(PGRAPHState *pg, Error **errp)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    const XemuLibretroVk *s = &xemu_libretro_vk;

    const char *missing = xemu_vk_load_dispatch(s->get_instance_proc_addr,
                                                 s->instance, s->device);
    if (missing) {
        error_setg(errp, "Frontend Vulkan entry point unavailable: %s", missing);
        return false;
    }
    r->instance = s->instance;

    r->physical_device = s->gpu;
    vkGetPhysicalDeviceProperties(r->physical_device, &r->device_props);
    r->vk_api_version = MIN(s->api_version, r->device_props.apiVersion);
    if (r->vk_api_version < VK_API_VERSION_1_1) {
        error_setg(errp, "Vulkan 1.1 or higher is required");
        return false;
    }

    r->device = s->device;
    r->queue = s->queue;
    r->enabled_physical_device_features = s->features;
    r->debug_utils_extension_enabled = false;
    r->custom_border_color_extension_enabled = s->custom_border_color;
    r->memory_budget_extension_enabled = s->memory_budget;
    r->external_memory_host_extension_enabled = s->external_memory_host;
    r->eds_extension_enabled = r->dyn_cull_front = s->dyn_cull_front;
    r->eds3_extension_enabled = r->dyn_blend = s->dyn_blend;
    r->vertex_input_dynamic_extension_enabled = r->dyn_vertex_input =
        s->dyn_vertex_input;
    r->pipeline_library_extension_enabled = r->pipeline_library =
        s->pipeline_library;
    r->host_query_reset = s->host_query_reset;
    r->shader_demote = s->shader_demote;
    pgraph_vk_spirv_1_6 = r->shader_demote;
    init_float_controls(r);

    fprintf(stderr,
            "Vulkan device shared with the frontend: %s, queue family %u (%s)\n",
            r->device_props.deviceName, s->queue_family,
            s->queue_shared ? "the frontend's queue" : "a queue of its own");
    return true;
}

void pgraph_vk_init_instance(PGRAPHState *pg, Error **errp)
{
    if (xemu_libretro_vk.active) {
        if (adopt_frontend_device(pg, errp) && init_allocator(pg, errp)) {
            return;
        }
        pgraph_vk_finalize_instance(pg);
        error_prepend(errp, "Failed to initialize Vulkan renderer: ");
        return;
    }
    error_setg(errp, "Vulkan requires a device negotiated with the libretro frontend");
}

void pgraph_vk_finalize_instance(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (r->allocator != VK_NULL_HANDLE) {
        xemu_vk_allocator_destroy(r->allocator);
        r->allocator = VK_NULL_HANDLE;
    }

    /* The negotiated instance and device remain owned by the frontend. */
    r->device = VK_NULL_HANDLE;
    r->instance = VK_NULL_HANDLE;
}

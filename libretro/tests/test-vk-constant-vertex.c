/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define BUFFER_UNIFORM_STAGING 0
#define BUFFER_UNIFORM 1
#define BUFFER_VERTEX_INLINE 2
#define BUFFER_VERTEX_RAM 3
#define TARGET_PAGE_SIZE 4096
#define TARGET_PAGE_MASK (~(uint64_t)4095)
#define TARGET_PAGE_ALIGN(x) (((x) + 4095) & TARGET_PAGE_MASK)
#define ROUND_UP(x,a) (((x)+(a)-1)&~((VkDeviceSize)(a)-1))
typedef struct { VkDeviceSize total_size; } ShaderUniformLayout;
typedef struct { VkBuffer buffer; uint8_t *mapped; VkDeviceSize buffer_offset; } StorageBuffer;
typedef struct {
    bool dyn_vertex_input;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceProperties device_props;
    StorageBuffer storage_buffers[4];
    uint16_t constant_vertex_mask, constant_vertex_staged_mask;
    uint64_t uniform_upload_epoch, constant_vertex_staged_epoch;
    VkDeviceSize constant_vertex_staged_offset;
    unsigned num_active_vertex_binding_descriptions, num_active_vertex_attribute_descriptions;
    VkVertexInputBindingDescription vertex_binding_descriptions[16];
    VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
    VkDeviceSize vertex_attribute_offsets[16];
    bool vertex_arena_pending, vertex_arena_used, vertex_attr_cow[16];
    unsigned vertex_attr_cow_slot[16];
    VkDeviceSize vertex_attr_cow_offset[16];
    struct { VkBuffer buffer; } vertex_arena[2];
    struct { uint64_t addr, size; } vertex_attr_span[16];
    unsigned long vertex_consumed_bitmap[1];
    VkCommandBuffer command_buffer;
} PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; struct { float inline_value[4]; } vertex_attributes[16]; } PGRAPHState;
typedef struct { bool portability; } VkExtensionPropertiesArray;
#define g_autoptr(T) T *
static VkExtensionPropertiesArray device_extensions;
static unsigned capability_queries;
static VkExtensionPropertiesArray *get_available_device_extensions(VkPhysicalDevice device)
{ (void)device; capability_queries++; return &device_extensions; }
static bool is_extension_available(VkExtensionPropertiesArray *extensions, const char *name)
{ assert(!strcmp(name, "VK_KHR_portability_subset")); return extensions->portability; }
static unsigned copies, bind_count, bound_count;
static VkBuffer bound_buffers[16];
static VkDeviceSize bound_offsets[16];
static void bitmap_set(unsigned long *p, size_t a, size_t b) { (void)p; (void)a; assert(!b); }
static VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index, void **data,
                                              VkDeviceSize *sizes, size_t count, VkDeviceAddress alignment)
{
    StorageBuffer *b = &pg->vk_renderer_state->storage_buffers[index];
    assert(index == BUFFER_UNIFORM_STAGING && alignment == sizeof(float));
    VkDeviceSize start = b->buffer_offset;
    for (size_t i = 0; i < count; i++) {
        assert(sizes[i] == 16 && b->buffer_offset + sizes[i] <= 1024);
        memcpy(b->mapped + b->buffer_offset, data[i], sizes[i]);
        b->buffer_offset += sizes[i];
        copies++;
    }
    return start;
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer cmd, uint32_t first,
                                                 uint32_t count, const VkBuffer *buffers,
                                                 const VkDeviceSize *offsets)
{
    (void)cmd; assert(first == 0 && count <= 16); bind_count++; bound_count=count;
    memcpy(bound_buffers, buffers, count * sizeof(*buffers));
    memcpy(bound_offsets, offsets, count * sizeof(*offsets));
}
/* PRODUCTION */
int main(void)
{
    uint8_t bytes[1024], rotated[1024];
    PGRAPHVkState r = {0}; PGRAPHState pg = { .vk_renderer_state = &r };
    for (unsigned caps = 0; caps < 4; caps++) {
        r.dyn_vertex_input = caps & 1;
        device_extensions.portability = caps & 2;
        unsigned before = capability_queries;
        assert(constant_vertex_inputs_supported(&r) == !!(caps == 1));
        assert(capability_queries == before + !!(caps & 1));
    }
    for (unsigned mask = 0; mask <= 65535; mask++) {
        memset(&r, 0, sizeof(r)); memset(bytes, 0xa5, sizeof(bytes));
        r.uniform_upload_epoch = mask + 7;
        r.constant_vertex_mask = mask;
        r.storage_buffers[BUFFER_UNIFORM_STAGING] = (StorageBuffer){ .mapped = bytes, .buffer_offset = 32 };
        r.storage_buffers[BUFFER_UNIFORM].buffer = (VkBuffer)(uintptr_t)11;
        r.storage_buffers[BUFFER_VERTEX_INLINE].buffer = (VkBuffer)(uintptr_t)12;
        r.storage_buffers[BUFFER_VERTEX_RAM].buffer = (VkBuffer)(uintptr_t)13;
        r.device_props.limits.minUniformBufferOffsetAlignment = 1u << ((mask >> 8) % 9);
        ShaderUniformLayout layout0 = { 1 + (mask % 97) }, layout1 = { 1 + (mask % 127) };
        ShaderUniformLayout *layouts[2] = { &layout0, &layout1 };
        unsigned uploads = (mask >> 10) & 3;
        VkDeviceSize start = (mask * 17) & 1023, end = start;
        r.storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset = start;
        for (unsigned stage = 0; stage < 2; stage++) if (uploads & (1u << stage)) {
            while (end % r.device_props.limits.minUniformBufferOffsetAlignment) end++;
            end += layouts[stage]->total_size;
        }
        if (mask) {
            while (end % sizeof(float)) end++;
            for (unsigned a = 0; a < 16; a++) if (mask & (1u << a)) end += 16;
        }
        VkDeviceSize span = uniform_staging_span(&r, layouts, uploads, mask != 0);
        assert(start + span == end);
        assert((start + span <= 1024) == (end <= 1024));
        r.storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset = 32;
        for (unsigned a = 0; a < 16; a++) {
            for (unsigned k = 0; k < 4; k++) {
                uint32_t bits = k == 0 ? 0x80000000u : k == 1 ? 0x7fc00000u + a : (mask * 33 + a * 7 + k);
                memcpy(&pg.vertex_attributes[a].inline_value[k], &bits, 4);
            }
            r.vertex_attribute_offsets[a] = a * 32;
            if (!(mask & (1u << a))) {
                unsigned slot = r.num_active_vertex_binding_descriptions++;
                r.vertex_binding_descriptions[slot] = (VkVertexInputBindingDescription){slot, 64, VK_VERTEX_INPUT_RATE_VERTEX};
                r.vertex_attribute_descriptions[r.num_active_vertex_attribute_descriptions++] =
                    (VkVertexInputAttributeDescription){a,slot,VK_FORMAT_R32G32B32A32_SFLOAT,0};
            }
        }
        copies = 0;
        if (mask) {
            assert(!constant_vertex_snapshot_valid(&pg));
            stage_constant_vertex_values(&pg);
            assert(constant_vertex_snapshot_valid(&pg));
        }
        unsigned packed = 0;
        for (unsigned a = 0; a < 16; a++) if (mask & (1u << a)) {
            assert(!memcmp(bytes + 32 + packed * 16, pg.vertex_attributes[a].inline_value, 16));
            packed++;
        }
        assert(copies == packed);
        assert(r.storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset == 32 + packed * 16);
        bind_constant_vertex_descriptions(&pg);
        unsigned array_count = 16 - packed;
        assert(r.num_active_vertex_binding_descriptions == array_count + !!mask);
        assert(r.num_active_vertex_attribute_descriptions == 16);
        r.vertex_arena_pending = true;
        bind_vertex_buffer(&pg, 0xffff, 128);
        assert(!r.vertex_arena_pending);
        assert(bound_count == array_count + !!mask);
        packed = 0;
        for (unsigned slot = 0; slot < 16; slot++) {
            unsigned a = r.vertex_attribute_descriptions[slot].location;
            unsigned binding = r.vertex_attribute_descriptions[slot].binding;
            assert(binding < bound_count);
            if (mask & (1u << a)) {
                assert(binding == array_count);
                assert(r.vertex_binding_descriptions[binding].stride == 0);
                assert(r.vertex_attribute_descriptions[slot].format == VK_FORMAT_R32G32B32A32_SFLOAT);
                assert(bound_buffers[binding] == (VkBuffer)(uintptr_t)11);
                assert(bound_offsets[binding] == 32);
                assert(r.vertex_attribute_descriptions[slot].offset == packed++ * 16);
            } else {
                assert(binding == slot);
                assert(bound_buffers[binding] == (VkBuffer)(uintptr_t)12);
                assert(bound_offsets[binding] == 128 + a * 32);
            }
        }
        if (!mask) continue;
        unsigned a = 0; while (!(mask & (1u << a))) a++;
        ((uint8_t *)pg.vertex_attributes[a].inline_value)[0] ^= 1;
        assert(!constant_vertex_snapshot_valid(&pg));
        ((uint8_t *)pg.vertex_attributes[a].inline_value)[0] ^= 1;
        r.uniform_upload_epoch++;
        assert(!constant_vertex_snapshot_valid(&pg));
        r.uniform_upload_epoch--;
        r.storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset--;
        assert(!constant_vertex_snapshot_valid(&pg));
        r.storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset = 0;
        r.storage_buffers[BUFFER_UNIFORM_STAGING].mapped = rotated;
        r.uniform_upload_epoch++;
        assert(!constant_vertex_snapshot_valid(&pg));
        stage_constant_vertex_values(&pg);
        assert(constant_vertex_snapshot_valid(&pg));
        assert(r.constant_vertex_staged_offset == 0);
    }
    assert(bind_count == 65536);
    puts("Constant vertex owners: 65536 masks, exact rows/bindings, changed values, short spans, reset/rotation; no payload clone or heap allocation");
    return 0;
}

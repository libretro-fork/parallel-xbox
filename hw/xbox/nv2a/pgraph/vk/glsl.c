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

#include "renderer.h"
#include "ui/xemu-settings.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

/* Chosen at device creation (instance.c); SPIR-V 1.5 until then. */
bool pgraph_vk_spirv_1_6;

/* The GLSL the generators wrote, to SPIR-V: the core's own compiler
 * (pgraph/spv), tens of microseconds a shader, safe on any thread. A
 * shader it cannot compile is a bug in it or in a generator; the message
 * says which line. */
GByteArray *pgraph_vk_compile_glsl_to_spv(VkShaderStageFlagBits stage,
                                          const char *glsl_source)
{
    GlslcStage st;
    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT: st = GLSLC_STAGE_VERTEX; break;
    case VK_SHADER_STAGE_GEOMETRY_BIT: st = GLSLC_STAGE_GEOMETRY; break;
    case VK_SHADER_STAGE_FRAGMENT_BIT: st = GLSLC_STAGE_FRAGMENT; break;
    case VK_SHADER_STAGE_COMPUTE_BIT: st = GLSLC_STAGE_COMPUTE; break;
    default:
        assert(!"shader stage");
        return NULL;
    }
    GlslcOptions opts = { .spirv_1_6 = pgraph_vk_spirv_1_6 };
    GlslcResult result;
    if (!glslc_compile(st, glsl_source, &opts, &result)) {
        /* A bug in the compiler or a generator: the shader goes next to
         * the session log, to be sent with it. */
        char *path = g_strdup_printf("%sshader_compile_failed.glsl",
                                     xemu_settings_get_base_path());
        FILE *f = fopen(path, "w");
        if (f) {
            fputs(glsl_source, f);
            fclose(f);
        }
        fprintf(stderr,
                "nv2a: shader compilation failed, line %d: %s (the shader "
                "is in %s)\n",
                result.error_line, result.error, path);
        g_free(path);
        assert(!"shader compilation failed");
        return NULL;
    }
    /* the words were malloc'ed by the compiler: the array takes them */
    return g_byte_array_new_take((guint8 *)result.words,
                                 result.num_words * sizeof(uint32_t));
}

VkShaderModule pgraph_vk_create_shader_module_from_spv(PGRAPHVkState *r, GByteArray *spv)
{
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spv->len,
        .pCode = (uint32_t *)spv->data,
    };
    VkShaderModule module;
    VK_CHECK(
        vkCreateShaderModule(r->device, &create_info, NULL, &module));
    return module;
}

static void block_to_uniforms(const SpvReflectBlockVariable *block, ShaderUniformLayout *layout)
{
    assert(!layout->uniforms);

    layout->num_uniforms = block->member_count;
    layout->uniforms = g_malloc0_n(block->member_count, sizeof(ShaderUniform));
    layout->total_size = block->size;
    layout->allocation = g_malloc0(block->size);

    for (uint32_t k = 0; k < block->member_count; ++k) {
        const SpvReflectBlockVariable *member = &block->members[k];

        assert(member->array.dims_count < 2);

        int dim = 1;
        for (int i = 0; i < member->array.dims_count; i++) {
            dim *= member->array.dims[i];
        }
        int stride = MAX(member->array.stride, member->numeric.matrix.stride);
        if (member->numeric.matrix.column_count) {
            dim *= member->numeric.matrix.column_count;
            if (member->array.stride) {
                stride =
                    member->array.stride / member->numeric.matrix.column_count;
            }
        }
        layout->uniforms[k] = (ShaderUniform){
            .name = strdup(member->name),
            .offset = member->offset,
            .dim_v = MAX(1, member->numeric.vector.component_count),
            .dim_a = dim,
            .stride = stride,
        };

        // fprintf(stderr, "<%s offset=%zd dim_v=%zd dim_a=%zd stride=%zd>\n",
        //     layout->uniforms[k].name,
        //     layout->uniforms[k].offset,
        //     layout->uniforms[k].dim_v,
        //     layout->uniforms[k].dim_a,
        //     layout->uniforms[k].stride
        //     );
    }
    // fprintf(stderr, "--\n");
}

static void init_layout_from_spv(ShaderModuleInfo *info, GByteArray *spirv)
{
    /* Borrow immutable words while extracting the layouts. Neither parser
     * metadata nor source words are needed by the resulting Vulkan module. */
    SpvReflectShaderModule reflect;
    SpvReflectResult result = spvReflectCreateShaderModule2(
        SPV_REFLECT_MODULE_FLAG_NO_COPY, spirv->len, spirv->data, &reflect);
    assert(result == SPV_REFLECT_RESULT_SUCCESS &&
           "Failed to create SPIR-V shader module");

    for (uint32_t i = 0; i < reflect.descriptor_binding_count; i++) {
        const SpvReflectDescriptorBinding *binding = &reflect.descriptor_bindings[i];
        if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            block_to_uniforms(&binding->block, &info->uniforms);
        }
    }
    assert(reflect.push_constant_block_count < 2);
    if (reflect.push_constant_block_count) {
        block_to_uniforms(&reflect.push_constant_blocks[0], &info->push_constants);
    }
    spvReflectDestroyShaderModule(&reflect);
}

/* A module from SPIR-V the caller hands over (a worker compiled it) */
ShaderModuleInfo *pgraph_vk_create_shader_module_from_spirv(PGRAPHVkState *r,
                                                             GByteArray *spirv)
{
    ShaderModuleInfo *info = g_malloc0(sizeof(*info));
    info->refcnt = 0;
    info->module = pgraph_vk_create_shader_module_from_spv(r, spirv);
    init_layout_from_spv(info, spirv);
    g_byte_array_unref(spirv);
    return info;
}

ShaderModuleInfo *pgraph_vk_create_shader_module_from_glsl(
    PGRAPHVkState *r, VkShaderStageFlagBits stage, const char *glsl)
{
    return pgraph_vk_create_shader_module_from_spirv(
        r, pgraph_vk_compile_glsl_to_spv(stage, glsl));
}

static void finalize_uniform_layout(ShaderUniformLayout *layout)
{
    for (int i = 0; i < layout->num_uniforms; i++) {
        free((void*)layout->uniforms[i].name);
    }
    if (layout->uniforms) {
        g_free(layout->uniforms);
    }
    g_free(layout->allocation);
}

void pgraph_vk_ref_shader_module(ShaderModuleInfo *info)
{
    info->refcnt++;
}

void pgraph_vk_unref_shader_module(PGRAPHVkState *r, ShaderModuleInfo *info)
{
    assert(info->refcnt >= 1);

    info->refcnt--;
    if (info->refcnt == 0) {
        pgraph_vk_destroy_shader_module(r, info);
    }
}

void pgraph_vk_destroy_shader_module(PGRAPHVkState *r, ShaderModuleInfo *info)
{
    assert(info->refcnt == 0);
    finalize_uniform_layout(&info->uniforms);
    finalize_uniform_layout(&info->push_constants);
    vkDestroyShaderModule(r->device, info->module, NULL);
    g_free(info);
}

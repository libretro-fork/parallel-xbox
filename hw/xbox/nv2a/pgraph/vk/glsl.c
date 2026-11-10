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
#include <retro_atomic.h>

/* Chosen at device creation (instance.c); SPIR-V 1.5 until then. */
bool pgraph_vk_spirv_1_6;
bool pgraph_vk_preserve_float32;

/* Fallback for stages not yet emitted directly as SPIR-V. Compiler state
 * is local to this call; it creates no Vulkan objects. Cache misses may run
 * here synchronously when the worker has not prepared a module. Compilation
 * cost depends on the shader and is not a bounded draw-time guarantee. */
static GlslcResult compile_glsl(VkShaderStageFlagBits stage,
                                const char *glsl_source, bool emit_layout)
{
    GlslcStage st;
    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT: st = GLSLC_STAGE_VERTEX; break;
    case VK_SHADER_STAGE_GEOMETRY_BIT: st = GLSLC_STAGE_GEOMETRY; break;
    case VK_SHADER_STAGE_FRAGMENT_BIT: st = GLSLC_STAGE_FRAGMENT; break;
    case VK_SHADER_STAGE_COMPUTE_BIT: st = GLSLC_STAGE_COMPUTE; break;
    default:
        assert(!"shader stage");
        abort();
    }
    GlslcOptions opts = { .spirv_1_6 = pgraph_vk_spirv_1_6,
                          .emit_layout = emit_layout, .module_prefix_words = 256 };
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
        abort();
    }
    return result;
}

ShaderSource *pgraph_vk_shader_source_new(uint32_t *words, size_t num_words)
{
    ShaderSource *source = g_malloc0(sizeof(*source));
    retro_atomic_int_init(&source->refs, 1);
    source->words = words;
    source->num_words = num_words;
    source->len = sizeof(*source) + num_words * sizeof(*words);
    return source;
}

ShaderSource *pgraph_vk_shader_source_ref(ShaderSource *source)
{
    retro_atomic_fetch_add_int(&source->refs, 1);
    return source;
}

void pgraph_vk_shader_layouts_unref(ShaderLayouts *layouts)
{
    if (layouts && retro_atomic_fetch_sub_int(&layouts->refs, 1) == 1) {
        free(layouts->uniforms.members);
        free(layouts->push_constants.members);
        g_free(layouts);
    }
}

void pgraph_vk_shader_source_unref(ShaderSource *source)
{
    if (source && retro_atomic_fetch_sub_int(&source->refs, 1) == 1) {
        bool layouts_own_source = source->layouts_own_source;
        free(source->words_allocation ? source->words_allocation : source->words);
        /* An inline header can survive with module-held layout references.
         * Its word lend has ended; keep no stale payload pointers in it. */
        source->words = NULL;
        source->words_allocation = NULL;
        source->num_words = 0;
        pgraph_vk_shader_layouts_unref(source->layouts);
        if (!layouts_own_source) {
            g_free(source);
        }
    }
}

/* Move final word/layout owners into the existing immutable shared source.
 * There is no payload copy; the caller's emptied result owns nothing. */
ShaderSource *pgraph_vk_shader_source_take(GlslcResult *result)
{
    if (!result->uniforms.count && !result->push_constants.count) {
        ShaderSource *source = pgraph_vk_shader_source_new(result->words,
                                                          result->num_words);
        source->words_allocation = result->allocation;
        source->len = sizeof(*source) + result->allocation_words * sizeof(*result->words);
        memset(result, 0, sizeof(*result));
        return source;
    }
    /* Layouts own this allocation through the last module consumer. The
     * adjacent source header has independent refs and releases words first. */
    _Static_assert(sizeof(ShaderLayouts) % _Alignof(ShaderSource) == 0,
                   "source header must stay aligned after layouts");
    ShaderLayouts *layout_owner = g_malloc0(sizeof(*layout_owner) + sizeof(ShaderSource));
    ShaderSource *source = (ShaderSource *)(layout_owner + 1);
    retro_atomic_int_init(&source->refs, 1);
    source->layouts_own_source = true;
    source->words = result->words;
    source->num_words = result->num_words;
    source->words_allocation = result->allocation;
    source->len = sizeof(*source) + result->allocation_words * sizeof(*result->words);
    source->layouts = layout_owner;
    retro_atomic_int_init(&layout_owner->refs, 1);
    source->layouts->uniforms = result->uniforms;
    source->layouts->push_constants = result->push_constants;
    source->len += sizeof(*source->layouts);
    const GlslcLayout *layouts[] = { &result->uniforms, &result->push_constants };
    for (unsigned i = 0; i < ARRAY_SIZE(layouts); i++) {
        source->len += layouts[i]->count * sizeof(GlslcUniform);
        for (size_t j = 0; j < layouts[i]->count; j++) {
            source->len += strlen(layouts[i]->members[j].name) + 1;
        }
    }
    memset(result, 0, sizeof(*result));
    return source;
}

ShaderSource *pgraph_vk_compile_shader(VkShaderStageFlagBits stage,
                                      const char *glsl_source)
{
    GlslcResult result = compile_glsl(stage, glsl_source, true);
    return pgraph_vk_shader_source_take(&result);
}

VkShaderModule pgraph_vk_create_shader_module_from_words(PGRAPHVkState *r,
                                                  const uint32_t *words,
                                                  size_t num_words)
{
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = num_words * sizeof(*words),
        .pCode = words,
    };
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(r->device, &create_info, NULL, &module));
    return module;
}

/* Fixed programs have a host-defined layout. Borrow the compiler's words
 * through vkCreateShaderModule, then release them without reflecting or
 * allocating ShaderModuleInfo and unused uniform metadata. */
VkShaderModule pgraph_vk_create_fixed_shader_module_from_glsl(
    PGRAPHVkState *r, VkShaderStageFlagBits stage, const char *glsl)
{
    GlslcResult result = compile_glsl(stage, glsl, false);
    VkShaderModule module = pgraph_vk_create_shader_module_from_words(
        r, result.words, result.num_words);
    glslc_result_free_words(&result);
    return module;
}

/* Compiler metadata is immutable and may be shared with the worker memo.
 * Vulkan modules share names/descriptors and own mutable parameter storage. */
static void init_uniform_layout(ShaderUniformLayout *layout,
                                const GlslcLayout *source, void *storage)
{
    if (!source->count) {
        return;
    }
    layout->num_uniforms = source->count;
    layout->total_size = source->size;
    layout->uniforms = source->members;
    layout->allocation = storage;
}

/* Mutable parameter bytes share the module's lifetime, never another module's.
 * Keep both regions 16-byte aligned, including scalar-only push constants. */
static ShaderModuleInfo *alloc_shader_module_info(const ShaderLayouts *layouts)
{
    if (!layouts || (!layouts->uniforms.count &&
                     !layouts->push_constants.count)) {
        return g_malloc0(sizeof(ShaderModuleInfo));
    }
    size_t uniform_size = layouts && layouts->uniforms.count ?
                          layouts->uniforms.size : 0;
    size_t push_size = layouts && layouts->push_constants.count ?
                       layouts->push_constants.size : 0;
    size_t uniform_offset = (sizeof(ShaderModuleInfo) + 15) & ~(size_t)15;
    assert(uniform_size <= SIZE_MAX - uniform_offset - 15);
    size_t push_offset = (uniform_offset + uniform_size + 15) & ~(size_t)15;
    assert(push_size <= SIZE_MAX - push_offset);
    ShaderModuleInfo *info = g_malloc0(push_offset + push_size);
    if (layouts) {
        init_uniform_layout(&info->uniforms, &layouts->uniforms,
                            (char *)info + uniform_offset);
        init_uniform_layout(&info->push_constants, &layouts->push_constants,
                            (char *)info + push_offset);
    }
    return info;
}

/* A module from SPIR-V the caller hands over (a worker compiled it) */
ShaderModuleInfo *pgraph_vk_create_shader_module_from_spirv(PGRAPHVkState *r,
                                                             ShaderSource *spirv)
{
    ShaderModuleInfo *info = alloc_shader_module_info(spirv->layouts);
    info->refcnt = 0;
    assert(r->next_shader_module_identity != UINT64_MAX);
    info->identity = ++r->next_shader_module_identity;
    info->module = pgraph_vk_create_shader_module_from_words(
        r, spirv->words, spirv->num_words);
    info->layouts = spirv->layouts;
    if (info->layouts) {
        retro_atomic_fetch_add_int(&info->layouts->refs, 1);
    }
    pgraph_vk_shader_source_unref(spirv);
    return info;
}

ShaderModuleInfo *pgraph_vk_create_shader_module_from_glsl(
    PGRAPHVkState *r, VkShaderStageFlagBits stage, const char *glsl)
{
    return pgraph_vk_create_shader_module_from_spirv(
        r, pgraph_vk_compile_shader(stage, glsl));
}

/* Word lend ends when vkCreateShaderModule returns. Names/descriptors are
 * borrowed from the immutable core image through module teardown; mutable
 * parameter bytes live in the final module owner. Never pass transient metadata. */
ShaderModuleInfo *pgraph_vk_create_static_shader_module(
    PGRAPHVkState *r, const uint32_t *words, size_t num_words,
    const ShaderUniform *push_constants, size_t count, size_t size)
{
    assert((count != 0) == (size != 0));
    assert(!count || push_constants);
    size_t offset = (sizeof(ShaderModuleInfo) + 15) & ~(size_t)15;
    assert(size <= SIZE_MAX - offset);
    ShaderModuleInfo *info = g_malloc0(offset + size);
    info->push_constants = (ShaderUniformLayout){
        .uniforms=push_constants, .num_uniforms=count, .total_size=size,
        .allocation=size ? (char *)info + offset : NULL,
    };
    assert(r->next_shader_module_identity != UINT64_MAX);
    info->identity = ++r->next_shader_module_identity;
    info->module = pgraph_vk_create_shader_module_from_words(r, words, num_words);
    return info;
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
    pgraph_vk_shader_layouts_unref(info->layouts);
    vkDestroyShaderModule(r->device, info->module, NULL);
    g_free(info);
}

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

/* Fixed words belong to the core image. The driver borrows them only through
 * vkCreateShaderModule; no compiler/result/layout owner is needed. */
VkShaderModule pgraph_vk_create_utility_shader_module(
    PGRAPHVkState *r, enum SpvUtilityShader shader)
{
    SpvStaticModule module = pgraph_spv_utility(shader, pgraph_vk_spirv_1_6);
    assert(module.words && module.num_words);
    return pgraph_vk_create_shader_module_from_words(r, module.words, module.num_words);
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

/* Unlinked stage objects share exactly the renderer's descriptor layout and
 * supported vertex push range. Source words are a synchronous creation lend. */
ShaderModuleInfo *pgraph_vk_create_shader_object_from_spirv(PGRAPHVkState *r,
                                     VkShaderStageFlagBits stage,
                                     ShaderSource *source, bool optional)
{
    assert(source); /* immutable native_kind may outlive optional route publication */
    VkPushConstantRange push = { .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0, .size = NV2A_VERTEXSHADER_ATTRIBUTES * 4 * sizeof(float) };
    VkShaderCreateInfoEXT ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .stage = stage,
        .nextStage = stage == VK_SHADER_STAGE_VERTEX_BIT ?
            VK_SHADER_STAGE_FRAGMENT_BIT |
              (r->shader_objects.features.geometryShader ? VK_SHADER_STAGE_GEOMETRY_BIT : 0) :
            stage == VK_SHADER_STAGE_GEOMETRY_BIT ? VK_SHADER_STAGE_FRAGMENT_BIT : 0,
        .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
        .codeSize = source->num_words * sizeof(uint32_t), .pCode = source->words,
        .pName = "main", .setLayoutCount = 1, .pSetLayouts = &r->descriptor_set_layout,
        .pushConstantRangeCount = r->use_push_constants_for_uniform_attrs ? 1 : 0,
        .pPushConstantRanges = &push,
    };
    VkShaderEXT object = VK_NULL_HANDLE;
    VkResult status = vkCreateShadersEXT(r->device, 1, &ci, NULL, &object);
    if (status != VK_SUCCESS || object == VK_NULL_HANDLE) {
        if (object) vkDestroyShaderEXT(r->device, object, NULL);
        fprintf(stderr, "nv2a: shader-object stage %u unavailable (%d); ordinary route retained\n",
                stage, status);
        pgraph_vk_shader_source_unref(source);
        if (!optional) r->shader_objects.failed = true;
        return NULL;
    }
    ShaderModuleInfo *info = alloc_shader_module_info(source->layouts);
    assert(r->next_shader_module_identity != UINT64_MAX);
    info->identity = ++r->next_shader_module_identity;
    info->shader_object = object; info->stage = stage;
    info->layouts = source->layouts;
    if (info->layouts) retro_atomic_fetch_add_int(&info->layouts->refs, 1);
    pgraph_vk_shader_source_unref(source);
    r->shader_objects.live_objects++;
    return info;
}

/* Optional startup preparation is all-or-nothing. Compilation/native creation
 * failure releases its temporary owners and preserves the ordinary renderer. */
ShaderModuleInfo *pgraph_vk_create_optional_vertex_module(PGRAPHVkState *r,
                                                          const char *glsl, bool preserve_float32)
{
    GlslcOptions opts = { .spirv_1_6 = pgraph_vk_spirv_1_6,
                          .preserve_float32 = preserve_float32,
                          .emit_layout = true, .module_prefix_words = 256 };
    GlslcResult result = {0};
    if (!glslc_compile(GLSLC_STAGE_VERTEX, glsl, &opts, &result)) {
        fprintf(stderr, "nv2a: prepared vertex unavailable: %s\n", result.error);
        glslc_result_free_words(&result);
        free(result.uniforms.members);
        free(result.push_constants.members);
        return NULL;
    }
    assert(r->shader_objects.enabled);
    return pgraph_vk_create_shader_object_from_spirv(r, VK_SHADER_STAGE_VERTEX_BIT,
                                 pgraph_vk_shader_source_take(&result), true);
}

/* Startup-only finite geometry preparation transfers final words/layouts. */
ShaderModuleInfo *pgraph_vk_create_optional_geometry_module(PGRAPHVkState *r,
                                                            const char *glsl)
{
    GlslcOptions opts = { .spirv_1_6 = pgraph_vk_spirv_1_6, .emit_layout = true };
    GlslcResult result = {0};
    if (!glslc_compile(GLSLC_STAGE_GEOMETRY, glsl, &opts, &result)) {
        fprintf(stderr, "nv2a: prepared geometry unavailable: %s\n", result.error);
        glslc_result_free_words(&result);
        free(result.uniforms.members); free(result.push_constants.members);
        return NULL;
    }
    return pgraph_vk_create_shader_object_from_spirv(r, VK_SHADER_STAGE_GEOMETRY_BIT,
                                  pgraph_vk_shader_source_take(&result), true);
}

/* Startup-only finite fragment preparation transfers the final source owner. */
ShaderModuleInfo *pgraph_vk_create_optional_fragment_module(PGRAPHVkState *r,
                                                            const char *glsl)
{
    GlslcOptions opts = { .spirv_1_6 = pgraph_vk_spirv_1_6, .emit_layout = true };
    GlslcResult result = {0};
    if (!glslc_compile(GLSLC_STAGE_FRAGMENT, glsl, &opts, &result)) {
        fprintf(stderr, "nv2a: prepared fragment unavailable: %s\n", result.error);
        glslc_result_free_words(&result);
        free(result.uniforms.members); free(result.push_constants.members);
        return NULL;
    }
    return pgraph_vk_create_shader_object_from_spirv(r, VK_SHADER_STAGE_FRAGMENT_BIT,
                                  pgraph_vk_shader_source_take(&result), true);
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
    assert(!info->object_gpu_ref);
    if (info->shader_object) {
        vkDestroyShaderEXT(r->device, info->shader_object, NULL);
        assert(r->shader_objects.live_objects);
        r->shader_objects.live_objects--;
    }
    if (info->module) vkDestroyShaderModule(r->device, info->module, NULL);
    g_free(info);
}

/* One intrusive reference per owner covers all recorded/submitted consumers.
 * The open ONE_TIME_SUBMIT recording belongs to the next submission serial. */
void pgraph_vk_shader_object_use(PGRAPHVkState *r, ShaderModuleInfo *module)
{
    assert(module && module->shader_object && r->in_command_buffer);
    module->object_retire_after = r->submit_count + 1;
    if (!module->object_gpu_ref) {
        pgraph_vk_ref_shader_module(module);
        module->object_gpu_ref = true;
        module->object_gpu_next = r->shader_objects.gpu_owners;
        r->shader_objects.gpu_owners = module;
    }
}

void pgraph_vk_shader_objects_retire(PGRAPHVkState *r)
{
    ShaderModuleInfo **link = &r->shader_objects.gpu_owners;
    while (*link) {
        ShaderModuleInfo *module = *link;
        if ((int32_t)(module->object_retire_after - r->settled_submit) <= 0) {
            *link = module->object_gpu_next;
            module->object_gpu_next = NULL; module->object_gpu_ref = false;
            pgraph_vk_unref_shader_module(r, module);
        } else link = &module->object_gpu_next;
    }
}

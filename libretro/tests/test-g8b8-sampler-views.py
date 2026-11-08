#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production GL sampler-view keys and reuse without a graphics driver."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / "hw/xbox/nv2a/pgraph/gl/texture.c").read_text()
header = (repo / "hw/xbox/nv2a/pgraph/gl/renderer.h").read_text()
shape = re.search(r"typedef struct TextureShape \{.*?\} TextureShape;",
                  (repo / "hw/xbox/nv2a/pgraph/texture.h").read_text(), re.S).group()
key = re.search(r"typedef struct TextureKey \{.*?\} TextureKey;", header, re.S).group()
binding = re.search(r"typedef struct TextureBinding \{.*?\} TextureBinding;", header, re.S).group()
legacy = (key + binding).replace("bool g8b8_lod_clamp;", "").replace(
    "TextureKey", "LegacyTextureKey").replace("TextureBinding", "LegacyTextureBinding")
predicate = re.search(r"^static bool g8b8_sampler_lod_clamp\(.*?^\}", source, re.S | re.M).group()
guard = re.search(r"if \(!pg->texture_dirty\[i\].*?\) \{", source, re.S).group()
parameters = re.search(r"/\* Linear textures don't support mipmapping \*/.*?^    \}",
                       source, re.S | re.M).group()
vk_source = (repo / "hw/xbox/nv2a/pgraph/vk/texture.c").read_text()
vk_lod0 = re.search(r"    bool mixed_g8b8_lod0 = .*?;", vk_source, re.S).group()
vk_clamped = re.search(r"    bool mixed_g8b8_clamped = .*?;", vk_source, re.S).group()
vk_nearest = re.search(r"    if \(mixed_g8b8_clamped(?: \|\| mixed_color_lod0)?\) \{.*?^    \}", vk_source, re.S | re.M).group()
vk_ceiling = re.search(r"\.maxLod = (.*?),\n\s+\.mipLodBias", vk_source, re.S).group(1)
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL sampler view line=%d\n",__LINE__); _Exit(1); } } while(0)
typedef uint64_t hwaddr;
typedef unsigned GLenum, GLuint;
typedef int GLint;
typedef float GLfloat;
static unsigned ctz32(uint32_t x) { return __builtin_ctz(x); }
#include "hw/xbox/nv2a/nv2a_regs.h"
/* TYPES */
/* LEGACY */
_Static_assert(sizeof(TextureKey) == sizeof(LegacyTextureKey), "key grew");
_Static_assert(offsetof(TextureKey, texture_vram_offset) == offsetof(LegacyTextureKey, texture_vram_offset), "key tail moved");
_Static_assert(sizeof(TextureBinding) == sizeof(LegacyTextureBinding), "binding grew");
_Static_assert(offsetof(TextureBinding, scale) == offsetof(LegacyTextureBinding, scale), "binding tail moved");
/* PREDICATE */
static bool reuse_gate(TextureBinding *tbind, bool dirty, bool g8b8_lod_clamp,
                       bool surf_to_tex_bytes, bool upload, bool has_surface) {
    struct { bool texture_dirty[1]; } pg_value = {{dirty}}, *pg = &pg_value;
    struct { bool upload_pending; } surface_value = {upload}, *surface = has_surface ? &surface_value : NULL;
    int i = 0;
    /* GUARD */
        return true;
    }
    return false;
}
#define GL_TEXTURE_BASE_LEVEL 0x813c
#define GL_TEXTURE_MAX_LEVEL 0x813d
#define GL_TEXTURE_MIN_LOD 0x813a
static unsigned calls, names[3];
static float values[3];
static void glTexParameterf(GLenum target, GLenum name, float value) {
    (void)target; assert(calls < 3); names[calls] = name; values[calls++] = value;
}
static void glTexParameteri(GLenum target, GLenum name, int value) {
    glTexParameterf(target, name, value);
}
static void sampler_parameters(TextureShape s, bool linear, bool g8b8_lod_clamp) {
    struct { bool linear; } f = {linear};
    GLenum gl_target = 0;
    /* PARAMETERS */
}
#define MIN(a, b) ((a) < (b) ? (a) : (b))
static float vk_clamped_maximum(TextureShape state, unsigned min_filter,
                                unsigned address, bool mipmap_en,
                                bool opposite, bool *nearest) {
    unsigned vk_min_filter = !(min_filter & 1);
    unsigned vk_mag_filter = opposite ? !vk_min_filter : vk_min_filter;
    bool linear = state.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8;
    unsigned mip_levels = linear ? 1 : state.levels;
    /* VK_LOD0 */
    bool mipmap_nearest = linear || mip_levels == 1 || min_filter == 3 || min_filter == 4;
    bool mixed_color_lod0 = false;
    /* VK_CLAMPED */
    /* VK_NEAREST */
    *nearest = mipmap_nearest;
    return /* VK_CEILING */;
}
int main(void) {
    TextureShape shape;
    memset(&shape, 0, sizeof(shape));
    shape.color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8;
    shape.dimensionality = 2; shape.levels = 16;
    shape.width = shape.height = 32768; shape.depth = 1;
    shape.max_mipmap_level = 15;
    unsigned transitions = 0, gates = 0;
    for (unsigned level = 1; level <= 15; level++) {
        shape.min_mipmap_level = level;
        for (unsigned a = 0; a < 16; a++) for (unsigned b = 0; b < 16; b++) {
            TextureKey ka, kb;
            memset(&ka, 0, sizeof(ka)); memset(&kb, 0, sizeof(kb));
            ka.state = kb.state = shape;
            ka.g8b8_lod_clamp = g8b8_sampler_lod_clamp(&shape, a << 16 | 0xa2000000u);
            kb.g8b8_lod_clamp = g8b8_sampler_lod_clamp(&shape, b << 16 | 0x51000000u);
            bool mip_a = a >= 3 && a <= 6, mip_b = b >= 3 && b <= 6;
            assert(ka.g8b8_lod_clamp == mip_a && kb.g8b8_lod_clamp == mip_b);
            assert(!!memcmp(&ka, &kb, sizeof(ka)) == (mip_a != mip_b));
            TextureBinding binding = {.g8b8_lod_clamp = mip_a};
            assert(reuse_gate(&binding, false, mip_b, false, false, true) == (mip_a == mip_b));
            transitions++;
        }
        TextureKey ordinary; LegacyTextureKey old;
        memset(&ordinary, 0, sizeof(ordinary)); memset(&old, 0, sizeof(old));
        ordinary.state = old.state = shape;
        ordinary.texture_vram_offset = old.texture_vram_offset = 0x100000;
        assert(!memcmp(&ordinary, &old, sizeof(old)));
        for (unsigned clamp = 0; clamp < 2; clamp++) {
            calls = 0; sampler_parameters(shape, false, clamp);
            assert(calls == (clamp ? 3u : 2u));
            assert(names[0] == GL_TEXTURE_BASE_LEVEL && values[0] == (clamp ? 0 : level));
            assert(names[calls - 1] == GL_TEXTURE_MAX_LEVEL && values[calls - 1] == 15);
            if (clamp) assert(names[1] == GL_TEXTURE_MIN_LOD && values[1] == level);
        }
    }
    for (unsigned bits = 0; bits < 128; bits++) {
        TextureBinding binding = {.g8b8_lod_clamp = bits & 1, .byte_reinterpret = !!(bits & 8)};
        bool dirty = bits & 2, requested = bits & 4, bytes = bits & 16, upload = bits & 32;
        bool has_surface = bits & 64;
        bool expected = !dirty && binding.g8b8_lod_clamp == requested &&
                        binding.byte_reinterpret == bytes && !(has_surface && upload);
        assert(reuse_gate(&binding, dirty, requested, bytes, upload, has_surface) == expected);
        gates++;
    }
    assert(!reuse_gate(NULL, false, false, false, false, false));
    shape.min_mipmap_level = 0; assert(!g8b8_sampler_lod_clamp(&shape, 3u << 16));
    shape.min_mipmap_level = 16; assert(!g8b8_sampler_lod_clamp(&shape, 3u << 16));
    shape.min_mipmap_level = 1;
    for (unsigned format = 0; format < 66; format++) {
        shape.color_format = format;
        assert(g8b8_sampler_lod_clamp(&shape, 3u << 16) == (format == 0x28));
    }
    shape.color_format = 0x28;
    shape.border = true; assert(!g8b8_sampler_lod_clamp(&shape, 3u << 16)); shape.border = false;
    shape.cubemap = true; assert(!g8b8_sampler_lod_clamp(&shape, 3u << 16)); shape.cubemap = false;
    shape.dimensionality = 3; assert(!g8b8_sampler_lod_clamp(&shape, 3u << 16));
    calls = 0; sampler_parameters(shape, true, false); assert(!calls);
    printf("PASS: %u sampler-view transitions, %u reuse gates, legacy layout/keys and GL parameters\n", transitions, gates);
    unsigned vk_cases = 0;
    for (unsigned filter = 0; filter < 8; filter++)
    for (unsigned u = 0; u < 8; u++) for (unsigned v = 0; v < 8; v++)
    for (unsigned enabled = 0; enabled < 2; enabled++)
    for (unsigned opposite = 0; opposite < 2; opposite++)
    for (unsigned minimum = 0; minimum < 3; minimum++)
    for (unsigned maximum = 0; maximum < 3; maximum++)
    for (unsigned bits = 0; bits < 16; bits++) {
        TextureShape s = {0};
        s.levels = 3;
        s.dimensionality = bits & 1 ? 3 : 2;
        s.cubemap = bits & 2;
        s.border = bits & 4;
        s.color_format = bits & 8 ? NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8 :
                                   NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8;
        s.min_mipmap_level = minimum; s.max_mipmap_level = maximum;
        bool clamped = enabled && opposite && !minimum && !maximum && !bits &&
                       filter >= 3 && filter <= 6 &&
                       u >= 1 && u <= 4 && v >= 1 && v <= 4;
        bool lod0 = bits == 8 && opposite && filter >= 1 && filter <= 6;
        bool nearest;
        float ceiling = vk_clamped_maximum(s, filter, u | v << 8, enabled,
                                            opposite, &nearest);
        assert(ceiling == (clamped || lod0 ? 0.25f : enabled ? maximum : 0.0f));
        assert(nearest == (!!(bits & 8) || filter == 3 || filter == 4 || clamped));
        vk_cases++;
    }
    for (unsigned filter = 0; filter < 8; filter++)
    for (unsigned u = 0; u < 8; u++) for (unsigned v = 0; v < 8; v++)
    for (unsigned enabled = 0; enabled < 2; enabled++)
    for (unsigned opposite = 0; opposite < 2; opposite++)
    for (unsigned bits = 0; bits < 16; bits++) {
        TextureShape s = {0};
        s.levels = 1;
        s.dimensionality = bits & 1 ? 3 : 2;
        s.cubemap = bits & 2; s.border = bits & 4;
        s.color_format = bits & 8 ? NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8 :
                                   NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8;
        bool lod0 = !(bits & 7) && opposite &&
                    ((filter == 1 || filter == 2) ||
                     ((bits & 8) && filter >= 3 && filter <= 6));
        bool clamped = !bits && opposite && enabled && filter >= 3 && filter <= 6 &&
                       u >= 1 && u <= 4 && v >= 1 && v <= 4;
        bool nearest;
        assert(vk_clamped_maximum(s, filter, u | v << 8, enabled, opposite, &nearest) ==
               (lod0 || clamped ? 0.25f : 0.0f));
        assert(nearest);
        vk_cases++;
    }
    printf("PASS: %u Vulkan base-only mixed-filter sampler policies\n", vk_cases);
}
'''.replace("/* TYPES */", shape + key + binding).replace("/* LEGACY */", legacy).replace(
    "/* PREDICATE */", predicate).replace("/* GUARD */", guard).replace("/* PARAMETERS */", parameters).replace(
    "/* VK_LOD0 */", vk_lod0).replace("/* VK_CLAMPED */", vk_clamped).replace("/* VK_NEAREST */", vk_nearest).replace("/* VK_CEILING */", vk_ceiling)
with tempfile.TemporaryDirectory(prefix="g8b8-views-") as folder:
    c = Path(folder) / "test.c"
    binary = Path(folder) / "test"
    c.write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv) > 2 else "cc", "-std=gnu11", "-O2",
                    "-Wall", "-Wextra", "-Werror", "-I", str(repo), str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    # A pending ordinary surface is stale too, not just a byte alias.
    old = '!(surface && surface->upload_pending)'
    assert old in code
    c.write_text(code.replace(old, '!(surf_to_tex_bytes && surface && surface->upload_pending)', 1))
    subprocess.run([sys.argv[2] if len(sys.argv) > 2 else "cc", "-std=gnu11", "-O2",
                    "-Wall", "-Wextra", "-Werror", "-I", str(repo), str(c), "-o", str(binary)], check=True)
    rejected = subprocess.run([str(binary)], capture_output=True)
    assert rejected.returncode == 1 and b'FAIL sampler view' in rejected.stderr
    print('Required negative control rejected: pending ordinary surface reused')

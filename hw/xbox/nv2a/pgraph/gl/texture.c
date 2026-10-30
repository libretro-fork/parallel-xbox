/*
 * Geforce NV2A PGRAPH OpenGL Renderer
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
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

#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "hw/xbox/nv2a/pgraph/s3tc.h"
#include "hw/xbox/nv2a/pgraph/texture.h"
#include "debug.h"
#include "renderer.h"
#include "hw/xbox/nv2a/pgraph/vram-ranges.h"
#include "ui/xemu-settings.h"
#include "data/segaboot_logo.rgba.h"

static TextureBinding *generate_texture(const TextureShape s,
                                        const uint8_t *texture_data,
                                        const uint8_t *palette_data, bool empty);
static void texture_binding_destroy(gpointer data);

struct pgraph_texture_possibly_dirty_struct {
    hwaddr addr, size;
};

static void mark_textures_possibly_dirty_visitor(Lru *lru, LruNode *node, void *opaque)
{
    struct pgraph_texture_possibly_dirty_struct *test =
        (struct pgraph_texture_possibly_dirty_struct *)opaque;

    struct TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    if (tnode->binding == NULL || tnode->possibly_dirty) {
        return;
    }

    bool overlapping = pgraph_vram_ranges_overlap(
        test->addr, test->size, tnode->key.texture_vram_offset,
        tnode->key.texture_length);
    overlapping |= pgraph_vram_ranges_overlap(
        test->addr, test->size, tnode->key.palette_vram_offset,
        tnode->key.palette_length);

    tnode->possibly_dirty |= overlapping;
}

void pgraph_gl_mark_textures_possibly_dirty(NV2AState *d,
    hwaddr addr, hwaddr size)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    if (!size) {
        return;
    }
    assert(pgraph_vram_range_valid(addr, size, memory_region_size(d->vram)));
    /* Dirty tracking clears whole pages, so invalidate every cached view
     * sharing those pages, including palettes. */
    hwaddr end = TARGET_PAGE_ALIGN(addr + size);
    addr &= TARGET_PAGE_MASK;
    assert(end <= memory_region_size(d->vram));

    struct pgraph_texture_possibly_dirty_struct test = {
        .addr = addr,
        .size = end - addr,
    };

    lru_visit_active(&r->texture_cache,
                     mark_textures_possibly_dirty_visitor,
                     &test);
}

static bool check_texture_dirty(NV2AState *d, hwaddr addr, hwaddr size)
{
    if (!size) {
        return false;
    }
    assert(pgraph_vram_range_valid(addr, size, memory_region_size(d->vram)));
    hwaddr end = TARGET_PAGE_ALIGN(addr + size);
    addr &= TARGET_PAGE_MASK;
    assert(end <= memory_region_size(d->vram));
    return memory_region_test_and_clear_dirty(d->vram, addr, end - addr,
                                              DIRTY_MEMORY_NV2A_TEX);
}

// Check if any of the pages spanned by the a texture are dirty.
static bool check_texture_possibly_dirty(NV2AState *d,
                                         hwaddr texture_vram_offset,
                                         unsigned int length,
                                         hwaddr palette_vram_offset,
                                         unsigned int palette_length)
{
    bool possibly_dirty = false;
    if (check_texture_dirty(d, texture_vram_offset, length)) {
        possibly_dirty = true;
        pgraph_gl_mark_textures_possibly_dirty(d, texture_vram_offset, length);
    }
    if (palette_length && check_texture_dirty(d, palette_vram_offset,
                                                     palette_length)) {
        possibly_dirty = true;
        pgraph_gl_mark_textures_possibly_dirty(d, palette_vram_offset,
                                            palette_length);
    }
    return possibly_dirty;
}

static void apply_texture_parameters(PGRAPHGLState *r,
                                     TextureBinding *binding,
                                     const BasicColorFormatInfo *f,
                                     unsigned int dimensionality,
                                     unsigned int filter,
                                     unsigned int address,
                                     bool is_bordered,
                                     uint32_t border_color,
                                     uint32_t max_anisotropy)
{
    unsigned int min_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIN);
    unsigned int mag_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MAG);
    unsigned int lod_bias =
        GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIPMAP_LOD_BIAS);
    unsigned int addru = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRU);
    unsigned int addrv = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRV);
    unsigned int addrp = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRP);

    if (f->linear) {
        /* somtimes games try to set mipmap min filters on linear textures.
             * this could indicate a bug... */
        switch (min_filter) {
        case NV_PGRAPH_TEXFILTER0_MIN_BOX_NEARESTLOD:
        case NV_PGRAPH_TEXFILTER0_MIN_BOX_TENT_LOD:
            min_filter = NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0;
            break;
        case NV_PGRAPH_TEXFILTER0_MIN_TENT_NEARESTLOD:
        case NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD:
            min_filter = NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0;
            break;
        }
    }

    if (min_filter != binding->min_filter) {
        glTexParameteri(binding->gl_target, GL_TEXTURE_MIN_FILTER,
                        pgraph_texture_min_filter_gl_map[min_filter]);
        binding->min_filter = min_filter;
    }
    if (mag_filter != binding->mag_filter) {
        glTexParameteri(binding->gl_target, GL_TEXTURE_MAG_FILTER,
                        pgraph_texture_mag_filter_gl_map[mag_filter]);
        binding->mag_filter = mag_filter;
    }
    if (lod_bias != binding->lod_bias) {
        binding->lod_bias = lod_bias;
        glTexParameterf(binding->gl_target, GL_TEXTURE_LOD_BIAS,
                        pgraph_convert_lod_bias_to_float(lod_bias));
    }

    /* Texture wrapping */
    assert(addru < ARRAY_SIZE(pgraph_texture_addr_gl_map));
    if (addru != binding->addru) {
        glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_S,
                        pgraph_texture_addr_gl_map[addru]);
        binding->addru = addru;
    }
    bool needs_border_color = binding->addru == NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    if (dimensionality > 1) {
        if (addrv != binding->addrv) {
            assert(addrv < ARRAY_SIZE(pgraph_texture_addr_gl_map));
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_T,
                            pgraph_texture_addr_gl_map[addrv]);
            binding->addrv = addrv;
        }
        needs_border_color = needs_border_color || binding->addrv == NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    }
    if (dimensionality > 2) {
        if (addrp != binding->addrp) {
            assert(addrp < ARRAY_SIZE(pgraph_texture_addr_gl_map));
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_R,
                            pgraph_texture_addr_gl_map[addrp]);
            binding->addrp = addrp;
        }
        needs_border_color = needs_border_color || binding->addrp == NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    }

    /* Shadowed like the parameters above: a new binding is zeroed and a max
     * anisotropy is never 0 (1 << clamp), so the first apply always sends. */
    if (r->supported_extensions.texture_filter_anisotropic &&
        max_anisotropy != binding->max_anisotropy) {
        glTexParameterf(binding->gl_target, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        max_anisotropy);
        binding->max_anisotropy = max_anisotropy;
    }

    if (!is_bordered && needs_border_color) {
        if (!binding->border_color_set || binding->border_color != border_color) {
            /* FIXME: Color channels might be wrong order */
            GLfloat gl_border_color[4];
            pgraph_argb_pack32_to_rgba_float(border_color, gl_border_color);
            glTexParameterfv(binding->gl_target, GL_TEXTURE_BORDER_COLOR,
                             gl_border_color);

            binding->border_color_set = true;
            binding->border_color = border_color;
        }
    }
}

/* After a bind outside bind_textures: every bind reaches the driver until
 * the flip resets the shadow. */
void pgraph_gl_tex_shadow_invalidate(PGRAPHGLState *r)
{
    r->tex_shadow_valid = false;
}

void pgraph_gl_tex_shadow_reset(PGRAPHGLState *r)
{
    memset(r->tex_shadow_name, 0, sizeof(r->tex_shadow_name));
    memset(r->tex_shadow_target, 0, sizeof(r->tex_shadow_target));
    r->tex_shadow_valid = true;
    /* Units are in an unknown state after an external bind: rebind all. */
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glBindTexture(GL_TEXTURE_1D, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_3D, 0);
    }
}

static bool can_reinterpret_argb_as_rgb565(PGRAPHState *pg,
                                          const SurfaceBinding *surface,
                                          const TextureShape *shape)
{
    PGRAPHGLState *r = pg->gl_renderer_state;
    if (!surface->color || surface->swizzle ||
        surface->shape.color_format != NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8 ||
        surface->shape.anti_aliasing != NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1 ||
        surface->fmt.gl_internal_format != GL_RGBA8 ||
        shape->color_format != NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5 ||
        shape->dimensionality != 2 || shape->cubemap || shape->border ||
        shape->levels != 1 || !surface->width || !surface->height ||
        (uint64_t)shape->width != (uint64_t)surface->width * 2 ||
        shape->height != surface->height ||
        (uint64_t)surface->pitch != (uint64_t)surface->width * 4 ||
        shape->pitch != surface->pitch) {
        return false;
    }
    /* A single image must own the whole range. Do not bypass the ordered
     * RAM materialization when another live/parked dirty view overlaps it. */
    SurfaceBinding *other;
    QTAILQ_FOREACH(other, &r->surfaces, entry) {
        if (other != surface && other->draw_dirty &&
            pgraph_vram_ranges_overlap(surface->vram_addr, surface->size,
                other->vram_addr, other->size)) {
            return false;
        }
    }
    QTAILQ_FOREACH(other, &r->parked, entry) {
        if (other != surface && (other->writeback_owed || other->draw_dirty) &&
            pgraph_vram_ranges_overlap(surface->vram_addr, surface->size,
                other->vram_addr, other->size)) {
            return false;
        }
    }
    return true;
}

void pgraph_gl_bind_textures(NV2AState *d)
{
    int i;
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    NV2A_GL_DGROUP_BEGIN("%s", __func__);

    for (i=0; i<NV2A_MAX_TEXTURES; i++) {
        bool enabled = pgraph_is_texture_enabled(pg, i);
        /* FIXME: What happens if texture is disabled but stage is active? */

        if (!enabled) {
            if (r->tex_shadow_valid && r->tex_shadow_name[i] == 0 &&
                r->tex_shadow_target[i] == 0) {
                continue;
            }
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glBindTexture(GL_TEXTURE_1D, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_3D, 0);
            r->tex_shadow_name[i] = 0;
            r->tex_shadow_target[i] = 0;
            continue;
        }
        glActiveTexture(GL_TEXTURE0 + i);

        uint32_t filter = pgraph_reg_r(pg, NV_PGRAPH_TEXFILTER0 + i*4);
        uint32_t address = pgraph_reg_r(pg, NV_PGRAPH_TEXADDRESS0 + i*4);
        uint32_t border_color = pgraph_reg_r(pg, NV_PGRAPH_BORDERCOLOR0 + i*4);
        uint32_t max_anisotropy =
            1 << (GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_TEXCTL0_0 + i*4),
                           NV_PGRAPH_TEXCTL0_0_MAX_ANISOTROPY));

        /* Check for unsupported features */
        if (filter & NV_PGRAPH_TEXFILTER0_ASIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_ASIGNED");
        if (filter & NV_PGRAPH_TEXFILTER0_RSIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_RSIGNED");
        if (filter & NV_PGRAPH_TEXFILTER0_GSIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_GSIGNED");
        if (filter & NV_PGRAPH_TEXFILTER0_BSIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_BSIGNED");

        TextureShape state = pgraph_get_texture_shape(pg, i);
        hwaddr texture_vram_offset, palette_vram_offset;
        size_t length, palette_length;

        length = pgraph_get_texture_length(pg, &state);
        texture_vram_offset = pgraph_get_texture_phys_addr(pg, i);
        palette_vram_offset = pgraph_get_texture_palette_phys_addr_length(pg, i, &palette_length);

        assert(pgraph_vram_range_valid(texture_vram_offset, length,
                                       memory_region_size(d->vram)));
        assert(pgraph_vram_range_valid(palette_vram_offset, palette_length,
                                       memory_region_size(d->vram)));
        bool is_indexed = (state.color_format ==
                NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8);
        /* Indexed expansion reads the palette on the CPU, even when its
         * bytes were produced by a GPU render target. Materialize it before
         * the cache fast path can accept a binding or consume dirty bits. */
        if (is_indexed) {
            SurfaceBinding *palette_surface;
            QTAILQ_FOREACH(palette_surface, &r->surfaces, entry) {
                if (pgraph_vram_ranges_overlap(palette_vram_offset,
                        palette_length, palette_surface->vram_addr,
                        palette_surface->size)) {
                    pgraph_gl_surface_download_if_dirty(d, palette_surface);
                }
            }
            pgraph_gl_parked_materialize_range(d, palette_vram_offset,
                                               palette_length);
        }
        bool possibly_dirty = false;
        bool possibly_dirty_checked = false;

        SurfaceBinding *surface = pgraph_gl_surface_get(d, texture_vram_offset);
        /* A cubemap whose six faces are live colour surfaces is filled by
         * GPU copies (render_cube_faces_to_texture). */
        SurfaceBinding *cube_faces[6] = { 0 };
        bool surf_to_tex_cube = false;
        int cube_draw_time = 0;
        if (surface && state.cubemap && state.levels == 1) {
            size_t face_len = length / 6;
            TextureShape flat = state;
            flat.cubemap = false;
            bool all = true;
            for (int fc = 0; fc < 6; fc++) {
                SurfaceBinding *fs = pgraph_gl_surface_get(
                    d, texture_vram_offset + fc * face_len);
                if (!fs || !fs->color || fs->width != state.width ||
                    fs->height != state.height ||
                    !pgraph_gl_check_surface_to_texture_compatibility(fs,
                                                                     &flat)) {
                    all = false;
                    break;
                }
                cube_faces[fc] = fs;
                if (fs->draw_time > cube_draw_time) {
                    cube_draw_time = fs->draw_time;
                }
            }
            surf_to_tex_cube = all;
        }
        bool surf_to_tex_bytes = surface &&
            can_reinterpret_argb_as_rgb565(pg, surface, &state);
        TextureBinding *tbind = r->texture_binding[i];
        if (!pg->texture_dirty[i] && tbind &&
            tbind->byte_reinterpret == surf_to_tex_bytes &&
            !(surf_to_tex_bytes && surface->upload_pending)) {
            bool reusable = false;
            if (surf_to_tex_cube) {
                reusable = tbind->draw_time == cube_draw_time;
            } else if (surface && tbind->draw_time == surface->draw_time) {
                reusable = true;
            } else if (!surface) {
                possibly_dirty = check_texture_possibly_dirty(
                        d,
                        texture_vram_offset,
                        length,
                        palette_vram_offset,
                        is_indexed ? palette_length : 0);
                possibly_dirty_checked = true;
                reusable = !possibly_dirty;
            }

            if (reusable) {
                TextureBinding *tb = r->texture_binding[i];
                if (!(r->tex_shadow_valid &&
                      r->tex_shadow_name[i] == tb->gl_texture &&
                      r->tex_shadow_target[i] == tb->gl_target)) {
                    glBindTexture(tb->gl_target, tb->gl_texture);
                    r->tex_shadow_name[i] = tb->gl_texture;
                    r->tex_shadow_target[i] = tb->gl_target;
                }
                apply_texture_parameters(r,
                                         r->texture_binding[i],
                                         &kelvin_color_format_info_map[state.color_format],
                                         state.dimensionality,
                                         filter,
                                         address,
                                         state.border,
                                         border_color,
                                         max_anisotropy);
                continue;
            }
        }

        /*
         * Check active surfaces to see if this texture was a render target
         */
        bool surf_to_tex = false;
        if (surf_to_tex_cube) {
            for (int fc = 0; fc < 6; fc++) {
                if (cube_faces[fc]->upload_pending) {
                    pgraph_gl_upload_surface_data(d, cube_faces[fc], false);
                }
            }
        } else if (surface != NULL) {
            surf_to_tex = surf_to_tex_bytes ||
                pgraph_gl_check_surface_to_texture_compatibility(surface, &state);

            if (surf_to_tex && surface->upload_pending) {
                pgraph_gl_upload_surface_data(d, surface, false);
            }
        }

        if (!surf_to_tex && !surf_to_tex_cube) {
            // FIXME: Restructure to support rendering surfaces to cubemap faces

            // Writeback any surfaces which this texture may index
            QTAILQ_FOREACH(surface, &r->surfaces, entry) {
                if (pgraph_vram_ranges_overlap(texture_vram_offset, length,
                        surface->vram_addr, surface->size)) {
                    pgraph_gl_surface_download_if_dirty(d, surface);
                }
            }
            /* Parked surfaces owing this range write it back. */
            pgraph_gl_parked_materialize_range(d, texture_vram_offset,
                                               length);
        }

        TextureKey key;
        memset(&key, 0, sizeof(TextureKey));
        key.state = state;
        key.byte_reinterpret = surf_to_tex_bytes;
        key.texture_vram_offset = texture_vram_offset;
        key.texture_length = length;
        if (is_indexed) {
            key.palette_vram_offset = palette_vram_offset;
            key.palette_length = palette_length;
        }

        // Search for existing texture binding in cache
        uint64_t tex_binding_hash = fast_hash((uint8_t*)&key, sizeof(key));
        LruNode *found = lru_lookup(&r->texture_cache,
                                     tex_binding_hash, &key);
        TextureLruNode *key_out = container_of(found, TextureLruNode, node);
        possibly_dirty |= (key_out->binding == NULL) || key_out->possibly_dirty;

        if (!surf_to_tex && !surf_to_tex_cube && !possibly_dirty_checked) {
            possibly_dirty |= check_texture_possibly_dirty(
                    d,
                    texture_vram_offset,
                    length,
                    palette_vram_offset,
                    is_indexed ? palette_length : 0);
        }

        // Calculate hash of texture data, if necessary
        void *texture_data = (char*)d->vram_ptr + texture_vram_offset;
        void *palette_data = (char*)d->vram_ptr + palette_vram_offset;

        uint64_t tex_data_hash = 0;
        if (!surf_to_tex && !surf_to_tex_cube && possibly_dirty) {
            tex_data_hash = fast_hash(texture_data,
                pgraph_texture_hash_length(state, length));
            if (is_indexed) {
                tex_data_hash ^= fast_hash(palette_data, palette_length);
            }
        }

        // Free existing binding, if texture data has changed
        bool must_destroy = (key_out->binding != NULL)
                            && possibly_dirty
                            && (key_out->binding->data_hash != tex_data_hash);
        if (must_destroy) {
            texture_binding_destroy(key_out->binding);
            key_out->binding = NULL;
        }

        if (key_out->binding == NULL) {
            // Must create the texture
            key_out->binding = generate_texture(state, texture_data, palette_data,
                                               surf_to_tex_bytes);
            if (key_out->binding->gl_target == GL_TEXTURE_2D) {
                r->fresh_tex[r->fresh_tex_w++ % ARRAY_SIZE(r->fresh_tex)] =
                    key_out->binding->gl_texture;
            }
            key_out->binding->data_hash = tex_data_hash;
            key_out->binding->scale = 1;
            /* DOGE easter egg: SEGABOOT's "Chihiro" logo (512x128 A4R4G4B4)
             * becomes the embedded image, keyed on the guest data hash, so
             * any other SEGABOOT version keeps its own logo. */
            if (xemu_doge_mode() && tex_data_hash == 0xec2bbfd4787e2b7dULL &&
                key_out->binding->gl_target == GL_TEXTURE_2D &&
                state.width == 512 && state.height == 128) {
                /* A new texture object, as some drivers keep the old
                 * storage on respecification; four times the texels, so
                 * the photo stays sharp when scaled. Sampler state follows
                 * at bind time. */
                GLuint logo_tex = 0;
                glGenTextures(1, &logo_tex);
                glBindTexture(GL_TEXTURE_2D, logo_tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                                state.levels - 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2048, 512, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, segaboot_logo_data);
                if (state.levels > 1) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
                GLenum logo_err = glGetError();
                if (logo_err == GL_NO_ERROR) {
                    glDeleteTextures(1, &key_out->binding->gl_texture);
                    key_out->binding->gl_texture = logo_tex;
                    fprintf(stderr, "nv2a: SEGABOOT logo replaced\n");
                } else {
                    glDeleteTextures(1, &logo_tex);
                    glBindTexture(GL_TEXTURE_2D, key_out->binding->gl_texture);
                    fprintf(stderr,
                            "nv2a: SEGABOOT logo swap refused: GL error 0x%x\n",
                            logo_err);
                }
            }
        } else {
            // Saved an upload! Reuse existing texture in graphics memory.
            glBindTexture(key_out->binding->gl_target,
                          key_out->binding->gl_texture);
        }
        r->tex_shadow_name[i] = key_out->binding->gl_texture;
        r->tex_shadow_target[i] = key_out->binding->gl_target;

        key_out->possibly_dirty = false;
        TextureBinding *binding = key_out->binding;
        binding->refcnt++;

        if (surf_to_tex_cube && binding->draw_time < cube_draw_time) {
            pgraph_gl_render_cube_faces_to_texture(d, cube_faces, binding,
                                                   &state, i);
            binding->draw_time = cube_draw_time;
            binding->scale = pg->surface_scale_factor;
        }
        if (surf_to_tex && binding->draw_time < surface->draw_time) {

            trace_nv2a_pgraph_surface_render_to_texture(
                surface->vram_addr, surface->width, surface->height);
            if (surf_to_tex_bytes) {
                pgraph_gl_reinterpret_surface_to_texture(d, surface, binding);
            } else {
                pgraph_gl_render_surface_to_texture(d, surface, binding,
                                                    &state, i);
            }
            binding->draw_time = surface->draw_time;
            /* Depth reads and byte reinterpretation use native-size snapshots. */
            binding->scale =
                (surf_to_tex_bytes || binding->s2t_format == GL_DEPTH_COMPONENT24)
                    ? 1 : pg->surface_scale_factor;
        }

        apply_texture_parameters(r,
                                 binding,
                                 &kelvin_color_format_info_map[state.color_format],
                                 state.dimensionality,
                                 filter,
                                 address,
                                 state.border,
                                 border_color,
                                 max_anisotropy);

        if (r->texture_binding[i]) {
            if (r->texture_binding[i]->gl_target != binding->gl_target) {
                glBindTexture(r->texture_binding[i]->gl_target, 0);
                r->tex_shadow_name[i] = 0;
                r->tex_shadow_target[i] = 0;
            }
            texture_binding_destroy(r->texture_binding[i]);
        }
        r->texture_binding[i] = binding;
        pg->texture_dirty[i] = false;
    }
    NV2A_GL_DGROUP_END();
}

static enum S3TC_DECOMPRESS_FORMAT
gl_internal_format_to_s3tc_enum(GLint gl_internal_format)
{
    switch (gl_internal_format) {
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        return S3TC_DECOMPRESS_FORMAT_DXT1;
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        return S3TC_DECOMPRESS_FORMAT_DXT3;
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        return S3TC_DECOMPRESS_FORMAT_DXT5;
    default:
        assert(!"Invalid gl_internal_format");
    }
}

/* The upload's scratch buffers, grown to what the largest level needs
 * (the first big texture of a game; nothing afterwards) */
static uint8_t *tex_scratch(PGRAPHGLState *r, int which, size_t size)
{
    if (r->tex_scratch_size[which] < size) {
        size_t grown = MAX(size, 2 * r->tex_scratch_size[which]);
        r->tex_scratch[which] = g_realloc(r->tex_scratch[which], grown);
        r->tex_scratch_size[which] = grown;
    }
    return r->tex_scratch[which];
}

/* EXT_texture_compression_s3tc alone only covers 2D images. VTC accepts
 * the Xbox's 4-deep block groups directly; slice reordering corrupts them. */
static void upload_compressed_volume(PGRAPHGLState *r, GLenum target,
                                      GLint level, GLenum format,
                                      const uint8_t *data, unsigned width,
                                      unsigned height, unsigned depth,
                                      size_t compressed_size)
{
    if (r->s3tc_3d_native) {
        glCompressedTexImage3D(target, level, format, width, height, depth, 0,
                               compressed_size, data);
    } else {
        uint8_t *converted = tex_scratch(r, 1, (size_t)width * height * depth * 4);
        s3tc_decompress_3d_into(gl_internal_format_to_s3tc_enum(format), data,
                               width, height, depth, converted);
        glTexImage3D(target, level, GL_RGBA8, width, height, depth, 0,
                     GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, converted);
    }
}

/* A swizzled level, unswizzled into the scratch, and converted there where
 * the host cannot sample its format (palette, YUV, R6G5B5): the pixels to
 * upload, their rows width * bpp apart */
static const uint8_t *unswizzle_level(PGRAPHGLState *r, const TextureShape *s,
                                      const uint8_t *data,
                                      const uint8_t *palette,
                                      unsigned int width, unsigned int height,
                                      unsigned int depth, unsigned int bpp)
{
    size_t row_pitch = (size_t)width * bpp, slice_pitch = row_pitch * height;
    uint8_t *unswizzled = tex_scratch(r, 0, slice_pitch * depth);
    if (depth == 1) {
        unswizzle_rect(data, width, height, unswizzled, row_pitch, bpp);
    } else {
        unswizzle_box(data, width, height, depth, unswizzled, row_pitch,
                      slice_pitch, bpp);
    }
    size_t converted_size =
        pgraph_converted_texture_size(*s, width, height, depth);
    if (!converted_size) {
        return unswizzled;
    }
    uint8_t *converted = tex_scratch(r, 1, converted_size);
    pgraph_convert_texture_data_into(*s, unswizzled, palette, width, height,
                                     depth, row_pitch, slice_pitch,
                                     converted);
    return converted;
}

static void upload_gl_texture(GLenum gl_target,
                              const TextureShape s,
                              const uint8_t *texture_data,
                              const uint8_t *palette_data)
{
    PGRAPHGLState *r = g_nv2a->pgraph.gl_renderer_state;
    ColorFormatInfo f = kelvin_color_format_gl_map[s.color_format];
    nv2a_profile_inc_counter(NV2A_PROF_TEX_UPLOAD);
    /* Unswizzled/converted levels are tightly packed, including narrow
     * mip rows and RGB8 conversion output. Guest linear pitch is supplied
     * through ROW_LENGTH below, without implicit host row padding. */
    GLint unpack_alignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    unsigned int adjusted_width = s.width;
    unsigned int adjusted_height = s.height;
    unsigned int adjusted_pitch = s.pitch;
    unsigned int adjusted_depth = s.depth;
    if (!f.linear && s.border) {
        adjusted_width = MAX(16, adjusted_width * 2);
        adjusted_height = MAX(16, adjusted_height * 2);
        adjusted_pitch = adjusted_width * (s.pitch / s.width);
        adjusted_depth = MAX(16, s.depth * 2);
    }

    switch(gl_target) {
    case GL_TEXTURE_1D:
        assert(!"Invalid 1D gl target texture");
        break;
    case GL_TEXTURE_2D:
        if (f.linear) {
            /* Can't handle strides unaligned to pixels */
            assert(s.pitch % f.bytes_per_pixel == 0);

            /* straight from the Xbox's RAM, its pitch's texels to a row,
             * unless the format has to be converted (into the scratch) */
            size_t converted_size = pgraph_converted_texture_size(
                s, adjusted_width, adjusted_height, 1);
            const uint8_t *pixels = texture_data;
            if (converted_size) {
                uint8_t *converted = tex_scratch(r, 1, converted_size);
                pgraph_convert_texture_data_into(
                    s, texture_data, palette_data, adjusted_width,
                    adjusted_height, 1, adjusted_pitch, 0, converted);
                pixels = converted;
            }
            glPixelStorei(GL_UNPACK_ROW_LENGTH,
                          converted_size ? 0 : adjusted_pitch / f.bytes_per_pixel);
            glTexImage2D(GL_TEXTURE_2D, 0, f.gl_internal_format,
                         adjusted_width, adjusted_height, 0,
                         f.gl_format, f.gl_type, pixels);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            break;
        }
        /* fallthru */
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z: {

        unsigned int width = adjusted_width, height = adjusted_height;

        int level;
        for (level = 0; level < s.levels; level++) {
            width = MAX(width, 1);
            height = MAX(height, 1);

            if (f.gl_format == 0) { /* compressed */
                 // https://docs.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression#virtual-size-versus-physical-size
                unsigned int block_size =
                    f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ?
                        8 : 16;
                unsigned int physical_width = (width + 3) & ~3,
                             physical_height = (height + 3) & ~3;
                size_t level_size =
                    physical_width / 4 * physical_height / 4 * block_size;
                bool cropped = s.cubemap && adjusted_width != s.width;

                if (r->s3tc_native && !cropped) {
                    /* the blocks as they lie in the Xbox's RAM */
                    glCompressedTexImage2D(gl_target, level,
                                           f.gl_internal_format, width,
                                           height, 0, level_size,
                                           texture_data);
                } else {
                    uint8_t *converted =
                        tex_scratch(r, 1, (size_t)width * height * 4);
                    s3tc_decompress_2d_into(
                        gl_internal_format_to_s3tc_enum(f.gl_internal_format),
                        texture_data, width, height, converted);
                    unsigned int tex_width = width;
                    unsigned int tex_height = height;

                    if (cropped) {
                        // FIXME: Consider preserving the border.
                        // There does not seem to be a way to reference the border
                        // texels in a cubemap, so they are discarded.
                        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 4);
                        glPixelStorei(GL_UNPACK_SKIP_ROWS, 4);
                        tex_width = s.width;
                        tex_height = s.height;
                        if (physical_width == width) {
                            glPixelStorei(GL_UNPACK_ROW_LENGTH, adjusted_width);
                        }
                    }

                    /* Preserve every decoded channel bit, as the 3D path
                     * does; an unsized format leaves storage to the driver. */
                    glTexImage2D(gl_target, level, GL_RGBA8, tex_width,
                                 tex_height, 0, GL_RGBA,
                                 GL_UNSIGNED_INT_8_8_8_8_REV, converted);
                    if (cropped) {
                        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
                        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
                        if (physical_width == width) {
                            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                        }
                    }
                }
                texture_data += level_size;
            } else {
                const uint8_t *pixel_data =
                    unswizzle_level(r, &s, texture_data, palette_data, width,
                                    height, 1, f.bytes_per_pixel);
                unsigned int tex_width = width;
                unsigned int tex_height = height;

                if (s.cubemap && adjusted_width != s.width) {
                    size_t converted_size =
                        pgraph_converted_texture_size(s, width, height, 1);
                    unsigned int upload_bpp = converted_size
                        ? converted_size / ((size_t)width * height)
                        : f.bytes_per_pixel;
                    unsigned int pitch = width * upload_bpp;
                    // FIXME: Consider preserving the border.
                    // There does not seem to be a way to reference the border
                    // texels in a cubemap, so they are discarded.
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, adjusted_width);
                    tex_width = s.width;
                    tex_height = s.height;
                    pixel_data += 4 * upload_bpp + 4 * pitch;
                }

                glTexImage2D(gl_target, level, f.gl_internal_format, tex_width,
                             tex_height, 0, f.gl_format, f.gl_type,
                             pixel_data);
                if (s.cubemap && s.border) {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                }

                texture_data += width * height * f.bytes_per_pixel;
            }

            width /= 2;
            height /= 2;
        }

        break;
    }
    case GL_TEXTURE_3D: {

        unsigned int width = adjusted_width;
        unsigned int height = adjusted_height;
        unsigned int depth = adjusted_depth;

        assert(f.linear == false);

        int level;
        for (level = 0; level < s.levels; level++) {
            if (f.gl_format == 0) { /* compressed */
                width = MAX(width, 1);
                height = MAX(height, 1);
                unsigned int physical_width = (width + 3) & ~3,
                             physical_height = (height + 3) & ~3;
                depth = MAX(depth, 1);

                unsigned int block_size;
                if (f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                    block_size = 8;
                } else {
                    block_size = 16;
                }

                size_t texture_size = physical_width/4 * physical_height/4 * depth * block_size;

                upload_compressed_volume(r, gl_target, level,
                    f.gl_internal_format, texture_data, width, height, depth,
                    texture_size);

                texture_data += texture_size;
            } else {
                width = MAX(width, 1);
                height = MAX(height, 1);
                depth = MAX(depth, 1);

                const uint8_t *pixels =
                    unswizzle_level(r, &s, texture_data, palette_data, width,
                                    height, depth, f.bytes_per_pixel);
                glTexImage3D(gl_target, level, f.gl_internal_format,
                             width, height, depth, 0,
                             f.gl_format, f.gl_type, pixels);

                texture_data += width * height * depth * f.bytes_per_pixel;
            }

            width /= 2;
            height /= 2;
            depth /= 2;
        }
        break;
    }
    default:
        assert(!"Invalid gl target texture type");
        break;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
}

static TextureBinding* generate_texture(const TextureShape s,
                                        const uint8_t *texture_data,
                                        const uint8_t *palette_data, bool empty)
{
    ColorFormatInfo f = kelvin_color_format_gl_map[s.color_format];

    /* Create a new opengl texture */
    GLuint gl_texture;
    glGenTextures(1, &gl_texture);

    GLenum gl_target;
    if (s.cubemap) {
        assert(f.linear == false);
        assert(s.dimensionality == 2);
        gl_target = GL_TEXTURE_CUBE_MAP;
    } else {
        if (f.linear) {
            gl_target = GL_TEXTURE_2D;
            assert(s.dimensionality == 2);
        } else {
            switch(s.dimensionality) {
            case 1: gl_target = GL_TEXTURE_1D; break;
            case 2: gl_target = GL_TEXTURE_2D; break;
            case 3: gl_target = GL_TEXTURE_3D; break;
            default:
                NV2A_GL_DPRINTF(true, "Invalid texture dimensionality: %u", (unsigned)s.dimensionality);
                assert(!"Invalid texture dimensionality");
                break;
            }
        }
    }

    glBindTexture(gl_target, gl_texture);

    NV2A_GL_DLABEL(GL_TEXTURE, gl_texture,
                   "offset: 0x%08lx, format: 0x%02X%s, %d dimensions%s, "
                   "width: %d, height: %d, depth: %d",
                   texture_data - g_nv2a->vram_ptr,
                   s.color_format, f.linear ? "" : " (SZ)",
                   s.dimensionality, s.cubemap ? " (Cubemap)" : "",
                   s.width, s.height, s.depth);

    if (empty) {
        GLint unpack;
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, s.width, s.height, 0,
                     GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
    } else if (gl_target == GL_TEXTURE_CUBE_MAP) {
        size_t length = pgraph_get_cubemap_face_length(&g_nv2a->pgraph, s);

        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                          s, texture_data + 0 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                          s, texture_data + 1 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                          s, texture_data + 2 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                          s, texture_data + 3 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                          s, texture_data + 4 * length, palette_data);
        upload_gl_texture(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
                          s, texture_data + 5 * length, palette_data);
    } else {
        upload_gl_texture(gl_target, s, texture_data, palette_data);
    }

    /* Linear textures don't support mipmapping */
    if (!f.linear) {
        glTexParameteri(gl_target, GL_TEXTURE_BASE_LEVEL,
            s.min_mipmap_level);
        glTexParameteri(gl_target, GL_TEXTURE_MAX_LEVEL,
            s.max_mipmap_level);
    }

    if (f.gl_swizzle_mask[0] != 0 || f.gl_swizzle_mask[1] != 0
        || f.gl_swizzle_mask[2] != 0 || f.gl_swizzle_mask[3] != 0) {
        glTexParameteriv(gl_target, GL_TEXTURE_SWIZZLE_RGBA,
                         (const GLint *)f.gl_swizzle_mask);
    }

    TextureBinding *ret =
        obj_pool_take(&g_nv2a->pgraph.gl_renderer_state->texture_pool);
    ret->gl_target = gl_target;
    ret->gl_texture = gl_texture;
    ret->refcnt = 1;
    ret->draw_time = empty ? -1 : 0;
    ret->data_hash = 0;
    ret->byte_reinterpret = empty;
    ret->min_filter = 0xFFFFFFFF;
    ret->mag_filter = 0xFFFFFFFF;
    ret->lod_bias = 0xFFFFFFFF;
    ret->addru = 0xFFFFFFFF;
    ret->addrv = 0xFFFFFFFF;
    ret->addrp = 0xFFFFFFFF;
    ret->border_color_set = false;
    return ret;
}

static void texture_binding_destroy(gpointer data)
{
    TextureBinding *binding = (TextureBinding *)data;
    assert(binding->refcnt > 0);
    binding->refcnt--;
    if (binding->refcnt == 0) {
        glDeleteTextures(1, &binding->gl_texture);
        obj_pool_give(&g_nv2a->pgraph.gl_renderer_state->texture_pool,
                      binding);
    }
}

/* functions for texture LRU cache */
static void texture_cache_entry_init(Lru *lru, LruNode *node, const void *key)
{
    TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    memcpy(&tnode->key, key, sizeof(TextureKey));

    tnode->binding = NULL;
    tnode->possibly_dirty = false;
}

static void texture_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    if (tnode->binding) {
        texture_binding_destroy(tnode->binding);
        tnode->binding = NULL;
        tnode->possibly_dirty = false;
    }
}

static bool texture_cache_entry_compare(Lru *lru, LruNode *node,
                                        const void *key)
{
    TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    return memcmp(&tnode->key, key, sizeof(TextureKey));
}

void pgraph_gl_init_textures(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    const size_t texture_cache_size = 512;
    /* The cache's entries, plus what draws still hold after an eviction */
    obj_pool_init(&r->texture_pool, sizeof(TextureBinding),
                  texture_cache_size + 64);
    lru_init(&r->texture_cache);
    r->texture_cache_entries = malloc(texture_cache_size * sizeof(TextureLruNode));
    assert(r->texture_cache_entries != NULL);
    for (int i = 0; i < texture_cache_size; i++) {
        lru_add_free(&r->texture_cache, &r->texture_cache_entries[i].node);
    }

    r->texture_cache.init_node = texture_cache_entry_init;
    r->texture_cache.compare_nodes = texture_cache_entry_compare;
    r->texture_cache.post_node_evict = texture_cache_entry_post_evict;

    for (int i = 0; i < 2; i++) {
        r->tex_scratch[i] = NULL;
        r->tex_scratch_size[i] = 0;
    }
    r->s3tc_native = epoxy_has_gl_extension("GL_EXT_texture_compression_s3tc");
    r->s3tc_3d_native = r->s3tc_native &&
        epoxy_has_gl_extension("GL_NV_texture_compression_vtc");
}

void pgraph_gl_finalize_textures(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        r->texture_binding[i] = NULL;
    }

    lru_flush(&r->texture_cache);
    free(r->texture_cache_entries);
    obj_pool_finalize(&r->texture_pool);

    r->texture_cache_entries = NULL;
    for (int i = 0; i < 2; i++) {
        g_free(r->tex_scratch[i]);
        r->tex_scratch[i] = NULL;
        r->tex_scratch_size[i] = 0;
    }
}

/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
 * Copyright (c) 2026 RÃƒÂ©da ChÃƒÂ©rif-Touil
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

/* Frozen completion builder from 106857c; LGPL-2.1-or-later, as shaders.c. */
static void vk_spec_compile_state(const ShaderState *st)
{
    ShaderModuleCacheKey keys[3];
    int n = vk_state_module_keys(st, vk_spec_push_constants, keys);
    uint64_t state_hash = xxh3_64((const uint8_t *)st, sizeof(*st), 0);
    for (int i = 0; i < n; i++) {
        uint64_t key_hash = xxh3_64((const uint8_t *)&keys[i], sizeof(keys[i]), 0);
        ShaderSource *spv = vk_spec_compile_module(&keys[i], key_hash);
        VkSpecDone d;
        memset(&d, 0, sizeof(d));
        d.key = keys[i];
        d.key_hash = key_hash;
        d.spirv = spv;
        d.state_hash = i == n - 1 ? state_hash : 0;
        /* The out ring holds three records for each state the render
         * thread may have queued and not drained (vk_spec_queue bounds
         * that), so it has room; were it ever short, the record is dropped
         * and the state stays drawn as its sibling. */
        if (retro_spsc_write(&vk_spec_out, &d, sizeof(d)) != sizeof(d)) {
            pgraph_vk_shader_source_unref(d.spirv);
        }
    }
}

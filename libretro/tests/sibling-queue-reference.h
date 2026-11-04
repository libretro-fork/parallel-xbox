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

/* Frozen queue policy from 6610643; LGPL-2.1-or-later, as shaders.c. */
static void reference_queue(uint64_t h, const ShaderState *st)
{
    if (!vk_spec_started) {
        if (!retro_spsc_init(&vk_spec_in, VK_SPEC_RING * sizeof(ShaderState)) ||
            !retro_spsc_init(&vk_spec_out, 3 * VK_SPEC_RING * sizeof(VkSpecDone)) ||
            !retro_eventcount_init(&vk_spec_ec) ||
            !retro_eventcount_init(&vk_spec_idle_ec)) {
            return; /* no memory: every state compiles where it is met */
        }
        for (unsigned i = 0; i < VK_SPEC_READY; i++) {
            vk_spec_ready_order[i] = i;
        }
        vk_spec_started = true;
        qemu_thread_create(&vk_spec_thread, "vk-firstmeet", vk_spec_worker_fn,
                           NULL, QEMU_THREAD_DETACHED);
    }
    if (vk_spec_find(h) >= 0) {
        return;
    }
    /* What landed is taken in first; then there is room for the state's
     * modules in the out ring only while fewer than the ring's states are
     * queued and undrained (a cancelled state leaves no record: its slot
     * frees with the tracks at quiesce). */
    vk_spec_drain();
    if (vk_spec_undrained >= VK_SPEC_RING) {
        return; /* full: the next meeting queues it */
    }
    for (unsigned i = 0; i < ARRAY_SIZE(vk_spec_track); i++) {
        if (!vk_spec_track[i].phase) {
            if (retro_spsc_write(&vk_spec_in, st, sizeof(*st)) != sizeof(*st)) {
                return; /* full: the next meeting queues it */
            }
            vk_spec_track[i].hash = h;
            vk_spec_track[i].phase = VK_SPEC_QUEUED;
            vk_spec_undrained++;
            retro_atomic_fetch_add_int(&vk_spec_inflight, 1);
            retro_eventcount_notify(&vk_spec_ec);
            break;
        }
    }
}

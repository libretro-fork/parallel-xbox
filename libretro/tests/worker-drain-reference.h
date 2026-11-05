/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Frozen insertion/drain policy before direct destination copies. */
static void vk_spec_ready_push(const VkSpecDone *done)
{
    unsigned slot;
    if (vk_spec_ready_n < VK_SPEC_READY) {
        slot = vk_spec_ready_order[vk_spec_ready_n++];
    } else {
        slot = vk_spec_ready_order[0];
        pgraph_vk_shader_source_unref(vk_spec_ready[slot].spirv);
        vk_spec_ready_order[VK_SPEC_READY] = slot;
        if (++vk_spec_ready_order == vk_spec_ready_order_storage + VK_SPEC_READY_HEADROOM) {
            memmove(vk_spec_ready_order_storage, vk_spec_ready_order,
                    VK_SPEC_READY * sizeof(vk_spec_ready_order[0]));
            vk_spec_ready_order = vk_spec_ready_order_storage;
        }
    }
    vk_spec_ready[slot] = *done;
}

static void vk_spec_drain(void)
{
    if (!vk_spec_started) {
        return;
    }
    VkSpecDone d;
    while (retro_spsc_read(&vk_spec_out, &d, sizeof(d)) == sizeof(d)) {
        vk_spec_ready_push(&d);
        if (d.state_hash) {
            vk_spec_undrained--;
            int t = vk_spec_find(d.state_hash);
            if (t >= 0) {
                vk_spec_track[t].phase = VK_SPEC_ADOPTED;
            }
        }
    }
}

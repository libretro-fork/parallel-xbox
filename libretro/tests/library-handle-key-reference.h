/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Frozen cc5bdeec35 driver-handle identities for forced-reuse controls. */
static uint64_t reference_pre_hash(PGRAPHVkState *r, VkShaderModule vsh,
                             VkShaderModule geom,
                             const RenderPassState *rp,
                             const VkPipelineRasterizationStateCreateInfo *raster,
                             const VkPipelineInputAssemblyStateCreateInfo *ia,
                             const VkPipelineVertexInputStateCreateInfo *vi)
{
    struct {
        uint64_t vsh, geom;
        RenderPassState rp;
        VkPipelineRasterizationStateCreateInfo raster;
        VkPipelineInputAssemblyStateCreateInfo ia;
        VkVertexInputBindingDescription bind[NV2A_VERTEXSHADER_ATTRIBUTES];
        VkVertexInputAttributeDescription attr[NV2A_VERTEXSHADER_ATTRIBUTES];
        uint32_t nb, na;
    } k;
    memset(&k, 0, sizeof(k));
    k.vsh = (uint64_t)(uintptr_t)vsh;
    k.geom = (uint64_t)(uintptr_t)geom;
    k.rp = *rp;
    k.raster = *raster;
    k.raster.pNext = NULL;
    if (r->dyn_cull_front) {
        /* Set per draw, so they must not identify a library; the topology
         * class does, a dynamic topology cannot leave it. */
        k.raster.cullMode = 0;
        k.raster.frontFace = 0;
        k.ia.topology = topology_class(ia->topology);
    } else {
        k.ia = *ia;
        k.ia.pNext = NULL;
    }
    if (!r->dyn_vertex_input) {
        k.nb = vi->vertexBindingDescriptionCount;
        k.na = vi->vertexAttributeDescriptionCount;
        memcpy(k.bind, vi->pVertexBindingDescriptions,
               k.nb * sizeof(k.bind[0]));
        memcpy(k.attr, vi->pVertexAttributeDescriptions,
               k.na * sizeof(k.attr[0]));
    }
    return fast_hash((const uint8_t *)&k, sizeof(k));
}

static uint64_t reference_frag_hash(PGRAPHVkState *r, VkShaderModule psh,
                              const VkPipelineDepthStencilStateCreateInfo *ds,
                              const VkPipelineColorBlendStateCreateInfo *cb,
                              const RenderPassState *rp, uint32_t push_size)
{
    struct {
        uint64_t psh;
        VkPipelineDepthStencilStateCreateInfo ds;
        VkPipelineColorBlendAttachmentState blend;
        RenderPassState rp;
        uint32_t has_zeta, attachments, blend_valid, push_size;
    } k;
    memset(&k, 0, sizeof(k));
    k.psh = (uint64_t)(uintptr_t)psh;
    k.push_size = push_size;
    if (ds) {
        k.has_zeta = 1;
        if (!r->dyn_cull_front) {
            k.ds = *ds;
            k.ds.pNext = NULL;
        }
    }
    k.rp = *rp;
    k.attachments = cb->attachmentCount;
    if (k.attachments && cb->pAttachments && !r->dyn_blend) {
        k.blend = cb->pAttachments[0];
        k.blend_valid = 1;
    }
    return fast_hash((const uint8_t *)&k, sizeof(k));
}

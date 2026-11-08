/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/fast-hash.h"
#include "renderer.h"
#include "sun-native.h"
#include "../sun-shadow-hooks.h"
#include "sun-shadow-receiver.h"

#define SUN_SIZE 4096
#define SUN_PIPELINES 256
#define SUN_SETS 64

typedef struct SunConstants {
    float eye_to_light[16], clip_to_shadow[16], raster[4], params[4];
} SunConstants;
typedef struct SunPipeline {
    PipelineKey key;
    uint64_t hash;
    VkPipeline receiver, caster;
    VkPipelineLayout receiver_layout, caster_layout;
    bool cutout;
    uint32_t last_submit;
    uint16_t texgen;
    bool texture_matrix;
} SunPipeline;

struct PgraphVkSunNative {
    NV2AState *d;
    PGRAPHVkState *r;
    const PGRAPHRenderer *base;
    PGRAPHRenderer renderer;
    PgraphSunFifo fifo;
    PgraphSunMarker markers[PGRAPH_SUN_FIFO_MAX_MARKERS];
    VkImage images[2]; XemuVkAllocation allocations[2];
    VkImageView views[2]; VkFramebuffer framebuffers[2];
    VkImageLayout layouts[2];
    VkRenderPass render_pass;
    VkSampler sampler;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    struct { VkBuffer buffer; VkDeviceSize size; unsigned map; VkDescriptorSet set; } sets[SUN_SETS];
    unsigned num_sets, num_pipelines;
    SunPipeline pipelines[SUN_PIPELINES];
    unsigned write_map, read_map, frame, casters, receivers;
    bool gpu_alive, ready, scene_open, map_written, receiver_bound, draw_ready, frame_failed;
    PgraphSunScene scene, previous_scene;
    float light[16], previous_light[16];
    SunConstants constants;
    VkDescriptorSet draw_set; uint32_t draw_offset;
    SunPipeline *draw_pipeline;
    VkBuffer buffers[16], index;
    VkDeviceSize offsets[16], index_offset;
    unsigned bindings;
};
static PgraphVkSunNative *active;

static void multiply(const float *a,const float *b,float *out)
{
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++) {
        double v=0;for(unsigned k=0;k<4;k++)v+=(double)a[i*4+k]*b[k*4+j];out[i*4+j]=v;
    }
}
static VkShaderModule compile_shader(PGRAPHVkState *r,GlslcStage stage,const char *source)
{
    GlslcResult code={0};GlslcOptions opts={0};VkShaderModule module=VK_NULL_HANDLE;
    if(glslc_compile(stage,source,&opts,&code)) {
        VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize=code.num_words*4,.pCode=code.words};
        vkCreateShaderModule(r->device,&ci,NULL,&module);
    } else error_report("DOAXVB sun shader: %.512s",code.error);
    glslc_result_free_words(&code);free(code.uniforms.members);free(code.push_constants.members);return module;
}
static void image_barrier(PgraphVkSunNative *s,VkCommandBuffer cmd,unsigned map,VkImageLayout layout,
    VkPipelineStageFlags src,VkAccessFlags read,VkPipelineStageFlags dst,VkAccessFlags write)
{
    VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=read,.dstAccessMask=write,.oldLayout=s->layouts[map],.newLayout=layout,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=s->images[map],.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(cmd,src,dst,0,0,NULL,0,NULL,1,&barrier);s->layouts[map]=layout;
}
static bool create_resources(PgraphVkSunNative *s)
{
    PGRAPHVkState *r=s->r;
    VkFormatProperties props;vkGetPhysicalDeviceFormatProperties(r->physical_device,VK_FORMAT_D16_UNORM,&props);
    VkFormatFeatureFlags required=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if((props.optimalTilingFeatures&required)!=required)return false;
    VkImageCreateInfo image={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_D16_UNORM,.extent={SUN_SIZE,SUN_SIZE,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    XemuVkMemoryRequest request={.host=false};
    for(unsigned i=0;i<2;i++) {
        if(xemu_vk_create_image(r->allocator,&image,&request,&s->images[i],&s->allocations[i])!=VK_SUCCESS)return false;
        VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=s->images[i],
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=image.format,.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
        if(vkCreateImageView(r->device,&view,NULL,&s->views[i])!=VK_SUCCESS)return false;
    }
    VkAttachmentDescription attachment={.format=image.format,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth={0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.pDepthStencilAttachment=&depth};
    VkSubpassDependency dependency={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,
        .srcStageMask=(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),.dstStageMask=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo pass={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,
        .subpassCount=1,.pSubpasses=&subpass,.dependencyCount=1,.pDependencies=&dependency};
    if(vkCreateRenderPass(r->device,&pass,NULL,&s->render_pass)!=VK_SUCCESS)return false;
    for(unsigned i=0;i<2;i++) {
        VkFramebufferCreateInfo fb={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=s->render_pass,
            .attachmentCount=1,.pAttachments=&s->views[i],.width=SUN_SIZE,.height=SUN_SIZE,.layers=1};
        if(vkCreateFramebuffer(r->device,&fb,NULL,&s->framebuffers[i])!=VK_SUCCESS)return false;
    }
    VkSamplerCreateInfo sampler={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.compareEnable=VK_TRUE,.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL};
    if(vkCreateSampler(r->device,&sampler,NULL,&s->sampler)!=VK_SUCCESS)return false;
    VkDescriptorSetLayoutBinding bindings[]={
        {.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,.descriptorCount=1,
            .stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT}};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};
    if(vkCreateDescriptorSetLayout(r->device,&li,NULL,&s->set_layout)!=VK_SUCCESS)return false;
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,SUN_SETS},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,SUN_SETS}};
    VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=SUN_SETS,.poolSizeCount=2,.pPoolSizes=sizes};
    return vkCreateDescriptorPool(r->device,&pool,NULL,&s->pool)==VK_SUCCESS;
}
static void destroy_resources(PgraphVkSunNative *s,bool settle)
{
    PGRAPHVkState *r=s->r;if(!r)return;
    if(settle)pgraph_vk_finish(&s->d->pgraph,VK_FINISH_REASON_FLUSH);
    for(unsigned i=0;i<s->num_pipelines;i++) {
        SunPipeline *p=&s->pipelines[i];
        if(p->receiver)vkDestroyPipeline(r->device,p->receiver,NULL);
        if(p->caster)vkDestroyPipeline(r->device,p->caster,NULL);
        if(p->receiver_layout)vkDestroyPipelineLayout(r->device,p->receiver_layout,NULL);
        if(p->caster_layout)vkDestroyPipelineLayout(r->device,p->caster_layout,NULL);
    }
    if(s->pool)vkDestroyDescriptorPool(r->device,s->pool,NULL);
    if(s->set_layout)vkDestroyDescriptorSetLayout(r->device,s->set_layout,NULL);
    if(s->sampler)vkDestroySampler(r->device,s->sampler,NULL);
    for(unsigned i=0;i<2;i++) {
        if(s->framebuffers[i])vkDestroyFramebuffer(r->device,s->framebuffers[i],NULL);
        if(s->views[i])vkDestroyImageView(r->device,s->views[i],NULL);
        if(s->images[i])xemu_vk_destroy_image(r->allocator,s->images[i],s->allocations[i]);
    }
    if(s->render_pass)vkDestroyRenderPass(r->device,s->render_pass,NULL);
    s->gpu_alive=false;s->r=NULL;
}
static VkDescriptorSet descriptor(PgraphVkSunNative *s)
{
    VkBuffer buffer=s->r->storage_buffers[BUFFER_UNIFORM].buffer;
    VkDeviceSize size=s->r->storage_buffers[BUFFER_UNIFORM].buffer_size;
    for(unsigned i=0;i<s->num_sets;i++)if(s->sets[i].buffer==buffer && s->sets[i].size==size && s->sets[i].map==s->read_map)return s->sets[i].set;
    if(s->num_sets==SUN_SETS)return VK_NULL_HANDLE;
    VkDescriptorSet set;VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=s->pool,.descriptorSetCount=1,.pSetLayouts=&s->set_layout};
    if(vkAllocateDescriptorSets(s->r->device,&ai,&set)!=VK_SUCCESS)return VK_NULL_HANDLE;
    VkDescriptorBufferInfo uniform={buffer,0,sizeof(SunConstants)};
    VkDescriptorImageInfo image={s->sampler,s->views[s->read_map],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet writes[]={
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,.pBufferInfo=&uniform},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&image}};
    vkUpdateDescriptorSets(s->r->device,2,writes,0,NULL);
    s->sets[s->num_sets].buffer=buffer;s->sets[s->num_sets].size=size;s->sets[s->num_sets].map=s->read_map;s->sets[s->num_sets++].set=set;
    return set;
}
static const char sun_block[]=
    "layout(set=1,binding=0,std140) uniform SunState { vec4 eyeLight[4];vec4 clipShadow[4];vec4 raster;vec4 params; } sun;\n"
    "layout(set=1,binding=1) uniform sampler2DShadow shadowMap;\n";

static VkShaderModule receiver_shader(PgraphVkSunNative *s)
{
    GenPshGlslOptions opts={.vulkan=true,.ubo_binding=PSH_UBO_BINDING,.tex_binding=PSH_TEX_BINDING};
    MString *source=pgraph_glsl_gen_psh(&s->r->shader_binding->state.psh,opts);
    const char *text=mstring_get_str(source),*main=strstr(text,"void main() {");
    if(!main){mstring_unref(source);return VK_NULL_HANDLE;}
    MString *functions=mstring_from_str(sun_block);mstring_append(functions,pgraph_vk_sun_receiver_glsl);
    g_string_insert(source->gstr,main-text,mstring_get_str(functions));mstring_unref(functions);
    text=mstring_get_str(source);const char *anchor=strstr(text,"vec4 pFog =");
    if(!anchor){mstring_unref(source);return VK_NULL_HANDLE;}
    const char *apply=
        "vec4 sunClip=vec4(unscaled_xy-sun.raster.xy,zvalue,1.0);\n"
        "vec4 sunPosition=sunClip.x*sun.clipShadow[0]+sunClip.y*sun.clipShadow[1]+sunClip.z*sun.clipShadow[2]+sunClip.w*sun.clipShadow[3];\n"
        "vec4 sunWhite=sunShade(vec4(1.0),sunPosition,sun.params);\n"
        "pD0.rgb*=sunWhite.rgb;pD1.rgb*=sunWhite.rgb;pB0.rgb*=sunWhite.rgb;pB1.rgb*=sunWhite.rgb;\n";
    g_string_insert(source->gstr,anchor-text,apply);
    VkShaderModule result=compile_shader(s->r,GLSLC_STAGE_FRAGMENT,mstring_get_str(source));mstring_unref(source);return result;
}
static bool skinning(unsigned skin,unsigned *matrices,bool *remainder)
{
    static const unsigned count[]={1,2,2,3,3,4,4};
    if(skin>=ARRAY_SIZE(count))return false;
    *matrices=count[skin];*remainder=skin==1||skin==3||skin==5;return true;
}
static bool caster_pipeline(PgraphVkSunNative *s,SunPipeline *p)
{
    PGRAPHVkState *r=s->r;unsigned matrices;bool remainder;
    if(!skinning(r->shader_binding->state.vsh.fixed_function.skinning,&matrices,&remainder))return false;
    bool cutout=p->cutout;
    MString *source=mstring_from_str("#version 450\nlayout(location=0) in vec4 position;\n"
        "layout(location=0) out vec3 uv;\n"
        "layout(set=0,binding=0,std140) uniform Native {vec4 c[192];} native;\n");
    if(matrices>1)mstring_append(source,"layout(location=1) in vec4 weight;\n");
    if(cutout)mstring_append(source,"layout(location=9) in vec4 texcoord;\n");
    mstring_append(source,sun_block);mstring_append(source,"void main(){vec4 eye=vec4(0.0);float remaining=1.0;\n");
    for(unsigned i=0;i<matrices;i++) {
        char weight[32];if(matrices==1)snprintf(weight,sizeof(weight),"1.0");
        else if(remainder&&i==matrices-1)snprintf(weight,sizeof(weight),"remaining");
        else snprintf(weight,sizeof(weight),"weight.%c","xyzw"[i]);
        unsigned c=8+8*i;
        mstring_append_fmt(source,"eye+=(position*mat4(native.c[%u],native.c[%u],native.c[%u],native.c[%u]))*%s;\n",c,c+1,c+2,c+3,weight);
        if(remainder&&i<matrices-1)mstring_append_fmt(source,"remaining-=%s;\n",weight);
    }
    mstring_append(source,cutout?"vec4 maskCoord=texcoord;\n":"vec4 maskCoord=vec4(0.0,0.0,0.0,1.0);\n");
    for(unsigned j=0;j<4;j++) {
        unsigned mode=(p->texgen>>(3*j))&7;
        if(mode==TEXGEN_EYE_LINEAR || mode==TEXGEN_OBJECT_LINEAR)
            mstring_append_fmt(source,"maskCoord.%c=dot(native.c[%u],%s);\n","xyzw"[j],
                NV_IGRAPH_XF_XFCTX_TG0MAT+j,mode==TEXGEN_EYE_LINEAR?"eye":"position");
    }
    if(p->texture_matrix) {
        unsigned c=NV_IGRAPH_XF_XFCTX_T0MAT;
        mstring_append_fmt(source,"maskCoord=maskCoord*mat4(native.c[%u],native.c[%u],native.c[%u],native.c[%u]);\n",c,c+1,c+2,c+3);
    }
    mstring_append(source,"uv=maskCoord.xyw;\n");
    mstring_append(source,"gl_Position=eye.x*sun.eyeLight[0]+eye.y*sun.eyeLight[1]+eye.z*sun.eyeLight[2]+eye.w*sun.eyeLight[3];}\n");
    VkShaderModule vsh=compile_shader(r,GLSLC_STAGE_VERTEX,mstring_get_str(source));mstring_unref(source);
    MString *fragment=mstring_from_str("#version 450\nlayout(location=0) in vec3 uv;\n");mstring_append(fragment,sun_block);
    if(cutout)mstring_append(fragment,"layout(set=0,binding=2) uniform sampler2D maskTexture;\n");
    mstring_append(fragment,"void main(){\n");
    if(cutout)mstring_append(fragment,
        "if(uv.z==0.0)discard;int a=int(round(textureLod(maskTexture,uv.xy/uv.z,0.0).a*255.0));int f=int(sun.raster.w);int r=int(sun.raster.z);bool keep=false;\n"
        "if(f==1)keep=a<r;else if(f==2)keep=a==r;else if(f==3)keep=a<=r;else if(f==4)keep=a>r;else if(f==5)keep=a!=r;else if(f==6)keep=a>=r;else if(f==7)keep=true;\nif(!keep)discard;\n");
    mstring_append(fragment,"}\n");VkShaderModule fsh=compile_shader(r,GLSLC_STAGE_FRAGMENT,mstring_get_str(fragment));mstring_unref(fragment);
    if(!vsh||!fsh){if(vsh)vkDestroyShaderModule(r->device,vsh,NULL);if(fsh)vkDestroyShaderModule(r->device,fsh,NULL);return false;}
    VkDescriptorSetLayout sets[]={r->descriptor_set_layout,s->set_layout};
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=2,.pSetLayouts=sets};
    bool result=vkCreatePipelineLayout(r->device,&li,NULL,&p->caster_layout)==VK_SUCCESS;
    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vsh,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fsh,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=r->num_active_vertex_binding_descriptions,.pVertexBindingDescriptions=r->vertex_binding_descriptions,
        .vertexAttributeDescriptionCount=r->num_active_vertex_attribute_descriptions,.pVertexAttributeDescriptions=r->vertex_attribute_descriptions};
    VkPrimitiveTopology topology=s->d->pgraph.primitive_mode==PRIM_TYPE_TRIANGLE_STRIP?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=topology};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1,
        .depthBiasEnable=VK_TRUE,.depthBiasConstantFactor=2,.depthBiasSlopeFactor=2};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL};
    VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,
        .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&raster,.pMultisampleState=&ms,
        .pDepthStencilState=&ds,.pColorBlendState=&blend,.pDynamicState=&dynamic,.layout=p->caster_layout,.renderPass=s->render_pass};
    if(result)result=vkCreateGraphicsPipelines(r->device,r->vk_pipeline_cache,1,&ci,NULL,&p->caster)==VK_SUCCESS;
    vkDestroyShaderModule(r->device,vsh,NULL);vkDestroyShaderModule(r->device,fsh,NULL);return result;
}
static SunPipeline *pipeline(PgraphVkSunNative *s)
{
    /* Native dynamic-input keys omit these arrays, but our caster has a
     * static input layout. Own its complete format/stride key. */
    PipelineKey owned_key=s->r->pipeline_binding->key;
    memset(owned_key.binding_descriptions,0,sizeof(owned_key.binding_descriptions));
    memset(owned_key.attribute_descriptions,0,sizeof(owned_key.attribute_descriptions));
    memcpy(owned_key.binding_descriptions,s->r->vertex_binding_descriptions,
        s->r->num_active_vertex_binding_descriptions*sizeof(owned_key.binding_descriptions[0]));
    memcpy(owned_key.attribute_descriptions,s->r->vertex_attribute_descriptions,
        s->r->num_active_vertex_attribute_descriptions*sizeof(owned_key.attribute_descriptions[0]));
    PipelineKey *key=&owned_key;uint64_t hash=fast_hash((const uint8_t*)key,sizeof(*key));
    uint32_t control=pgraph_reg_r(&s->d->pgraph,NV_PGRAPH_CONTROL_0);
    bool cutout=(control&NV_PGRAPH_CONTROL_0_ALPHATESTENABLE) && GET_MASK(control,NV_PGRAPH_CONTROL_0_ALPHAFUNC)!=7;
    uint16_t texgen=0;for(unsigned j=0;j<4;j++)texgen|=pgraph_texgen_mode(&s->d->pgraph,0,j)<<(3*j);
    bool texture_matrix=s->d->pgraph.texture_matrix_enable[0];
    for(unsigned i=0;i<s->num_pipelines;i++)if(s->pipelines[i].hash==hash && s->pipelines[i].cutout==cutout &&
        s->pipelines[i].texgen==texgen && s->pipelines[i].texture_matrix==texture_matrix &&
        !memcmp(&s->pipelines[i].key,key,sizeof(*key)))return &s->pipelines[i];
    SunPipeline *p=NULL;
    if(s->num_pipelines<SUN_PIPELINES)p=&s->pipelines[s->num_pipelines++];
    else for(unsigned i=0;i<s->num_pipelines;i++) {
        SunPipeline *candidate=&s->pipelines[i];
        if((int32_t)(candidate->last_submit-s->r->settled_submit)>0)continue;
        if(candidate->receiver)vkDestroyPipeline(s->r->device,candidate->receiver,NULL);
        if(candidate->caster)vkDestroyPipeline(s->r->device,candidate->caster,NULL);
        if(candidate->receiver_layout)vkDestroyPipelineLayout(s->r->device,candidate->receiver_layout,NULL);
        if(candidate->caster_layout)vkDestroyPipelineLayout(s->r->device,candidate->caster_layout,NULL);
        memset(candidate,0,sizeof(*candidate));p=candidate;break;
    }
    if(!p)return NULL;
    p->key=*key;p->hash=hash;p->cutout=cutout;p->texgen=texgen;p->texture_matrix=texture_matrix;
    VkShaderModule fragment=receiver_shader(s);
    if(fragment) {pgraph_vk_sun_receiver_pipeline(&s->d->pgraph,fragment,&p->receiver_layout,&p->receiver,s->set_layout);vkDestroyShaderModule(s->r->device,fragment,NULL);}
    caster_pipeline(s,p);return p;
}

static void clear_map(PgraphVkSunNative *s,VkCommandBuffer cmd,unsigned map)
{
    bool fresh=s->layouts[map]==VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier(s,cmd,map,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        fresh?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        fresh?0:VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearDepthStencilValue clear={1,0};VkImageSubresourceRange range={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
    vkCmdClearDepthStencilImage(cmd,s->images[map],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
    image_barrier(s,cmd,map,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
}
static bool eligible(PgraphVkSunNative *s,PGRAPHState *pg)
{
    PGRAPHVkState *r=s->r;
    if(!s->gpu_alive || s->fifo.failed || !s->fifo.current.valid || !r->shader_binding ||
        !r->color_binding || !r->zeta_binding || !r->shader_binding->state.vsh.is_fixed_function ||
        r->shader_binding->state.vsh.z_perspective || pg->clearing ||
        (pg->primitive_mode!=PRIM_TYPE_TRIANGLES && pg->primitive_mode!=PRIM_TYPE_TRIANGLE_STRIP))return false;
    unsigned inputs=0;
    for(unsigned i=0;i<r->num_active_vertex_attribute_descriptions;i++)
        inputs|=1u<<r->vertex_attribute_descriptions[i].location;
    if(!(inputs&1) || (r->shader_binding->state.vsh.fixed_function.skinning!=SKINNING_OFF && !(inputs&2)))return false;
    for(unsigned i=0;i<4;i++)
        if(((r->shader_binding->state.psh.shader_stage_program>>(5*i))&31)==PS_TEXTUREMODES_DOT_ZW)return false;
    uint32_t c=pgraph_reg_r(pg,NV_PGRAPH_CONTROL_0);
    if((c&(NV_PGRAPH_CONTROL_0_ZWRITEENABLE|NV_PGRAPH_CONTROL_0_ZENABLE))!=
        (NV_PGRAPH_CONTROL_0_ZWRITEENABLE|NV_PGRAPH_CONTROL_0_ZENABLE))return false;
    if(pgraph_reg_r(pg,NV_PGRAPH_BLEND)&NV_PGRAPH_BLEND_EN)return false;
    /* The supported game uses these primary backbuffer sizes. Reflection
     * targets must not nominate a mirrored camera as the scene's camera. */
    unsigned aa_x=1,aa_y=1;pgraph_apply_anti_aliasing_factor(pg,&aa_x,&aa_y);
    unsigned width=pg->surface_binding_dim.width/aa_x,height=pg->surface_binding_dim.height/aa_y;
    if((width!=640 && width!=720)||height!=480)return false;
    ShaderBinding *binding=r->shader_binding;int index=binding->vsh.uniform_locs[VshUniform_c];
    if(index<=0 || binding->vsh.module_info->uniforms.uniforms[index-1].offset)return false;
    if((c&NV_PGRAPH_CONTROL_0_ALPHATESTENABLE) && GET_MASK(c,NV_PGRAPH_CONTROL_0_ALPHAFUNC)!=7) {
        if(!(inputs&(1u<<9)))return false;
        const PshState *psh=&binding->state.psh;
        if(psh->dim_tex[0]!=2 || psh->rect_tex[0] || psh->tex_cubemap[0])return false;
        for(unsigned j=0;j<4;j++)if(pgraph_texgen_mode(pg,0,j)>TEXGEN_OBJECT_LINEAR)return false;
    }
    return true;
}
static bool prepare_scene(PgraphVkSunNative *s)
{
    PgraphSunScene *scene=&s->fifo.current.scene;
    if(s->scene_open)return !memcmp(scene,&s->scene,sizeof(*scene));
    float inverse[16];
    if(!pgraph_sun_inverse(scene->view,inverse)||!pgraph_sun_fit(scene,SUN_SIZE,64,20,120,s->light))return false;
    s->scene=*scene;s->scene_open=true;
    /* A cut invalidates temporal visibility; ordinary camera motion is
     * reprojected into the previous map's owned world-to-light matrix. */
    if(s->ready) {
        float old_inverse[16]={0};bool cut=!pgraph_sun_inverse(s->previous_scene.view,old_inverse);
        double distance=0,direction=0;
        for(unsigned i=0;i<3;i++) {double d=inverse[12+i]-old_inverse[12+i];distance+=d*d;direction+=(double)inverse[8+i]*old_inverse[8+i];}
        cut |= distance>64 || direction<0.7 || memcmp(scene->projection,s->previous_scene.projection,sizeof(scene->projection));
        if(cut)s->ready=false;
    }
    multiply(inverse,s->light,s->constants.eye_to_light);
    multiply(scene->inverse_view_projection,s->previous_light,s->constants.clip_to_shadow);
    return true;
}
void pgraph_vk_sun_receiver_bind(PGRAPHState *pg)
{
    PgraphVkSunNative *s=active;if(!s || pg!=&s->d->pgraph)return;
    s->draw_ready=s->receiver_bound=false;s->bindings=0;s->index=VK_NULL_HANDLE;s->draw_pipeline=NULL;
    memset(s->buffers,0,sizeof(s->buffers));memset(s->offsets,0,sizeof(s->offsets));
    if(!eligible(s,pg))return;
    if(!prepare_scene(s)){s->frame_failed=true;return;}
    PGRAPHVkState *r=s->r;VkDeviceSize alignment=r->device_props.limits.minUniformBufferOffsetAlignment;
    if(!pgraph_vk_buffer_has_space_for(pg,BUFFER_UNIFORM_STAGING,sizeof(SunConstants),alignment)){s->frame_failed=true;return;}
    s->draw_pipeline=pipeline(s);if(!s->draw_pipeline || !s->draw_pipeline->caster){s->frame_failed=true;return;}
    s->draw_set=descriptor(s);if(!s->draw_set){s->frame_failed=true;return;}
    /* Invert the ordered NV2A projection/viewport matrix, not an assumed
     * framebuffer NDC convention. Native fragment zvalue and unscaled_xy
     * live in precisely this matrix's window-coordinate space. */
    float projection[16],projection_inverse[16],view_inverse[16],eye_previous[16];
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)
        memcpy(&projection[i*4+j],&pg->vsh_constants[NV_IGRAPH_XF_XFCTX_CMAT0+j][i],4);
    if(r->shader_binding->state.vsh.fixed_function.skinning==SKINNING_OFF) {
        float model_view[16],inverse_model[16],composite[16];
        for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)
            memcpy(&model_view[i*4+j],&pg->vsh_constants[NV_IGRAPH_XF_XFCTX_MMAT0+j][i],4);
        if(!pgraph_sun_inverse(model_view,inverse_model)){s->frame_failed=true;return;}
        memcpy(composite,projection,sizeof(composite));multiply(inverse_model,composite,projection);
    }
    if(!pgraph_sun_inverse(projection,projection_inverse)||!pgraph_sun_inverse(s->scene.view,view_inverse)){s->frame_failed=true;return;}
    multiply(view_inverse,s->previous_light,eye_previous);
    multiply(projection_inverse,eye_previous,s->constants.clip_to_shadow);
    memcpy(s->constants.raster,&pg->vsh_constants[NV_IGRAPH_XF_XFCTX_VPOFF],8);
    s->constants.raster[2]=GET_MASK(pgraph_reg_r(pg,NV_PGRAPH_CONTROL_0),NV_PGRAPH_CONTROL_0_ALPHAREF);
    s->constants.raster[3]=GET_MASK(pgraph_reg_r(pg,NV_PGRAPH_CONTROL_0),NV_PGRAPH_CONTROL_0_ALPHAFUNC);
    /* Like the native-port experiment, lit models receive occluder shadows
     * from half a world unit toward the sun. Suppressing short-range self
     * shadowing avoids joint seams and temporal silhouette streaks. */
    bool model=r->shader_binding->state.vsh.fixed_function.lighting;
    s->constants.params[0]=model?0.0002f+0.5f/120.0f:0.0005f;
    s->constants.params[1]=s->ready?0.4f:0;
    s->constants.params[2]=s->constants.params[3]=(model?6.0f:1.0f)/SUN_SIZE;
    void *data=&s->constants;VkDeviceSize size=sizeof(s->constants);
    s->draw_offset=pgraph_vk_append_to_buffer(pg,BUFFER_UNIFORM_STAGING,&data,&size,1,alignment);
    s->draw_ready=true;
    s->draw_pipeline->last_submit=r->submit_count+1; /* the actual open native recording */
    if(s->ready && s->draw_pipeline->receiver) {
        vkCmdBindPipeline(r->command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,s->draw_pipeline->receiver);
        vkCmdBindDescriptorSets(r->command_buffer,VK_PIPELINE_BIND_POINT_GRAPHICS,s->draw_pipeline->receiver_layout,
            1,1,&s->draw_set,1,&s->draw_offset);
        s->receiver_bound=true;s->receivers++;
    }
}
void pgraph_vk_sun_borrow_streams(PGRAPHState *pg,const VkBuffer *buffers,const VkDeviceSize *offsets,unsigned count)
{
    PgraphVkSunNative *s=active;if(!s||pg!=&s->d->pgraph||!s->draw_ready||count>16)return;
    s->bindings=count;memcpy(s->buffers,buffers,count*sizeof(*buffers));memcpy(s->offsets,offsets,count*sizeof(*offsets));s->index=VK_NULL_HANDLE;
}
void pgraph_vk_sun_borrow_index(PGRAPHState *pg,VkBuffer buffer,VkDeviceSize offset)
{
    PgraphVkSunNative *s=active;if(!s||pg!=&s->d->pgraph||!s->draw_ready)return;s->index=buffer;s->index_offset=offset;
}
void pgraph_vk_sun_caster_record(PGRAPHState *pg)
{
    PgraphVkSunNative *s=active;if(!s||pg!=&s->d->pgraph)return;
    if(!s->draw_ready||!s->bindings) {
        if(s->receiver_bound)pgraph_vk_ensure_not_in_render_pass(pg);
        s->draw_ready=s->receiver_bound=false;return;
    }
    PGRAPHVkState *r=s->r;VkCommandBuffer cmd=r->command_buffer;
    pgraph_vk_ensure_not_in_render_pass(pg);
    if(!s->map_written) {
        /* Initialize the other descriptor image too, before it can ever be
         * sampled. No map copy or additional queue submission is needed. */
        unsigned other=s->write_map^1;
        if(s->layouts[other]==VK_IMAGE_LAYOUT_UNDEFINED) {
            clear_map(s,cmd,other);
            image_barrier(s,cmd,other,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        }
        clear_map(s,cmd,s->write_map);s->map_written=true;
    }
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=s->render_pass,
        .framebuffer=s->framebuffers[s->write_map],.renderArea.extent={SUN_SIZE,SUN_SIZE}};
    vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s->draw_pipeline->caster);
    VkViewport viewport={.width=SUN_SIZE,.height=SUN_SIZE,.maxDepth=1};VkRect2D scissor={.extent={SUN_SIZE,SUN_SIZE}};
    vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&scissor);
    uint32_t offsets[]={r->uniform_buffer_offsets[0],r->uniform_buffer_offsets[1],s->draw_offset};
    VkDescriptorSet sets[]={r->descriptor_sets[r->descriptor_set_index-1],s->draw_set};
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s->draw_pipeline->caster_layout,0,2,sets,3,offsets);
    vkCmdBindVertexBuffers(cmd,0,s->bindings,s->buffers,s->offsets);
    if(s->index) {
        vkCmdBindIndexBuffer(cmd,s->index,s->index_offset,VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd,pg->inline_elements_length,1,0,0,0);
    } else if(pg->draw_arrays_length) {
        for(unsigned i=0;i<pg->draw_arrays_length;i++)vkCmdDraw(cmd,pg->draw_arrays_count[i],1,pg->draw_arrays_start[i],0);
    } else if(pg->inline_buffer_length)vkCmdDraw(cmd,pg->inline_buffer_length,1,0,0);
    else if(pg->inline_array_length) {
        unsigned stride=0;for(unsigned i=0;i<16;i++)if(pg->vertex_attributes[i].count)stride+=pgraph_inline_attr_size(&pg->vertex_attributes[i]);
        if(stride)vkCmdDraw(cmd,pg->inline_array_length*4/stride,1,0,0);
    }
    vkCmdEndRenderPass(cmd);r->vi_last.valid=false;r->dyn_last.valid=false;s->casters++;
    /* End ALL input lends before a subsequent cache update, upload, eviction,
     * descriptor rewind or arena recycle can run. GPU reads are in the same
     * native recording, before those events, with its original retirement. */
    s->bindings=0;s->index=VK_NULL_HANDLE;s->draw_ready=s->receiver_bound=false;s->draw_set=VK_NULL_HANDLE;
    memset(s->buffers,0,sizeof(s->buffers));memset(s->offsets,0,sizeof(s->offsets));
}
static void frame_end(PgraphVkSunNative *s)
{
    if(s->map_written) {
        VkCommandBuffer cmd=pgraph_vk_begin_nondraw_commands(&s->d->pgraph);
        image_barrier(s,cmd,s->write_map,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT),VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        s->ready=!s->frame_failed;
        if(s->ready) {
            s->read_map=s->write_map;s->write_map^=1;
            memcpy(s->previous_light,s->light,sizeof(s->light));s->previous_scene=s->scene;
        }
    } else s->ready=false;
    if(s->frame<3 || s->frame%300==0)info_report("DOAXVB sun frame %u: %u casters, %u receivers, map=%u, geometry copies=0",
        s->frame,s->casters,s->receivers,s->map_written&&!s->frame_failed);
    s->frame++;s->scene_open=s->map_written=s->frame_failed=false;s->casters=s->receivers=0;
}
static void flip_stall(NV2AState *d)
{
    PgraphVkSunNative *s=active;frame_end(s);s->base->ops.flip_stall(d);
}
static void finalize(NV2AState *d)
{
    PgraphVkSunNative *s=active;const PGRAPHRenderer *base=s->base;
    destroy_resources(s,true);active=NULL;pgraph_sun_worker_failed();
    retro_atomic_store_release_ptr(&d->pgraph.renderer,(void*)base);base->ops.finalize(d);
}
PgraphVkSunNative *pgraph_vk_sun_create(NV2AState *d,uint64_t lifetime)
{
    const PGRAPHRenderer *base=pgraph_renderer(&d->pgraph);
    if(!base||base->type!=CONFIG_DISPLAY_RENDERER_VULKAN||!d->pgraph.vk_renderer_state)return NULL;
    PgraphVkSunNative *s=g_new0(PgraphVkSunNative,1);s->d=d;s->read_map=1;
    if(!pgraph_sun_fifo_start(true,&s->fifo,s->markers,PGRAPH_SUN_FIFO_MAX_MARKERS,PGRAPH_SUN_DOAXVB_US_11106,lifetime)) {
        g_free(s);return NULL;
    }
    return s;
}
PgraphSunFifo *pgraph_vk_sun_fifo(PgraphVkSunNative *s) {return &s->fifo;}
bool pgraph_vk_sun_enter(NV2AState *d,PgraphVkSunNative *s)
{
    s->base=pgraph_renderer(&d->pgraph);
    if(!s->base||s->base->type!=CONFIG_DISPLAY_RENDERER_VULKAN)return false;
    s->r=d->pgraph.vk_renderer_state;
    if(!create_resources(s)){destroy_resources(s,false);error_report("DOAXVB sun shadows: Vulkan resources unavailable");return false;}
    s->gpu_alive=true;s->renderer=*s->base;
    s->renderer.ops.flush_draw=pgraph_vk_sun_flush_draw;s->renderer.ops.draw_end=pgraph_vk_sun_draw_end;
    s->renderer.ops.flip_stall=flip_stall;s->renderer.ops.finalize=finalize;
    active=s;retro_atomic_store_release_ptr(&d->pgraph.renderer,&s->renderer);
    info_report("DOAXVB sun shadows enabled: previous-scene map, immediate native-stream casters");return true;
}
void pgraph_vk_sun_leave(NV2AState *d,PgraphVkSunNative *s)
{
    if(s->gpu_alive)destroy_resources(s,true);
    if(pgraph_renderer(&d->pgraph)==&s->renderer)retro_atomic_store_release_ptr(&d->pgraph.renderer,(void*)s->base);
    if(active==s)active=NULL;
}
void pgraph_vk_sun_free(PgraphVkSunNative *s)
{
    if(!s)return;
    assert(!s->gpu_alive);pgraph_sun_fifo_stop(&s->fifo);g_free(s);
}

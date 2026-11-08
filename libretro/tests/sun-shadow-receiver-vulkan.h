/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/nv2a/pgraph/vk/sun-shadow-receiver.h"

static float compare_texel(int x, int y, float reference)
{
    float depth = x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT ? 1 : (x < WIDTH / 2 ? .25f : .75f);
    return reference <= depth ? 1 : 0;
}
static float compare_sample(float u, float v, float reference, bool linear)
{
    if (!linear) return compare_texel((int)floorf(u * WIDTH), (int)floorf(v * HEIGHT), reference);
    float x = u * WIDTH - .5f, y = v * HEIGHT - .5f;
    int ix = (int)floorf(x), iy = (int)floorf(y); float fx = x - ix, fy = y - iy;
    float a = compare_texel(ix,iy,reference) * (1-fx) + compare_texel(ix+1,iy,reference) * fx;
    float b = compare_texel(ix,iy+1,reference) * (1-fx) + compare_texel(ix+1,iy+1,reference) * fx;
    return a * (1-fy) + b * fy;
}
static VkPipeline receiver_pipeline(VkShaderModule vertex, VkShaderModule fragment,
                                    VkPipelineLayout layout, VkRenderPass render)
{
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertex,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"},
    };
    VkPipelineVertexInputStateCreateInfo vi = {.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {0,0,WIDTH,HEIGHT,0,1}; VkRect2D scissor = {{0,0},{WIDTH,HEIGHT}};
    VkPipelineViewportStateCreateInfo vp = {.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo rs = {.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms = {.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ca = {.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo cb = {.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&ca};
    VkGraphicsPipelineCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,
        .pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&cb,.layout=layout,.renderPass=render};
    VkPipeline pipeline; CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&ci,NULL,&pipeline));
    return pipeline;
}
static void receiver_tests(VkRenderPass depth_render, VkFramebuffer depth_framebuffer, VkImage depth_image, VkImageView depth_view)
{
    const char *invalid_calls[]={
        "texture(shadowMap,vec2(0.5))", "textureLod(shadowMap,vec3(0.5))",
        "textureLod(shadowMap,vec3(0.5),vec2(0.0))",
        "texture(shadowMap,vec3(0.5),vec2(0.0))", "textureProj(shadowMap,vec4(0.5))",
    };
    for(unsigned i=0;i<sizeof(invalid_calls)/sizeof(invalid_calls[0]);i++) {
        char source[512];snprintf(source,sizeof(source),
            "#version 450\nlayout(set=0,binding=0) uniform sampler2DShadow shadowMap;layout(location=0) out vec4 color;void main(){float v=%s;color=vec4(v);}",invalid_calls[i]);
        GlslcResult rejected={0};GlslcOptions options={0};
        assert(!glslc_compile(GLSLC_STAGE_FRAGMENT,source,&options,&rejected));
        glslc_result_free_words(&rejected);
    }
    const char *vertex_source = "#version 450\nvoid main(){vec2 p=vec2(float((gl_VertexIndex<<1)&2),float(gl_VertexIndex&2));gl_Position=vec4(p*2.0-vec2(1.0),0.0,1.0);}";
    char fragment_source[8192];
    int n = snprintf(fragment_source,sizeof(fragment_source),
        "#version 450\nlayout(set=0,binding=0) uniform sampler2DShadow shadowMap;\n"
        "layout(push_constant) uniform Probe {vec4 color;vec4 clip;vec4 params;} probe;\n"
        "layout(location=0) out vec4 result;\n%s\nvoid main(){result=sunShade(probe.color,probe.clip,probe.params);}\n",
        pgraph_vk_sun_receiver_glsl);
    assert(n>0 && (size_t)n<sizeof(fragment_source));
    VkShaderModule vertex = compile_sun(GLSLC_STAGE_VERTEX,vertex_source);
    VkShaderModule fragments[3];
    fragments[0] = compile_sun(GLSLC_STAGE_FRAGMENT,fragment_source);
    /* Exercise explicit LOD and both implicit-LOD overloads with the same
     * independent comparison oracle. The receiver itself uses mip zero. */
    char *implicit_source = malloc(sizeof(fragment_source)); assert(implicit_source);
    memcpy(implicit_source,fragment_source,strlen(fragment_source)+1);
    char *sample = strstr(implicit_source,"textureLod("); assert(sample);
    size_t length = strlen(implicit_source), at = (size_t)(sample-implicit_source);
    assert(at+10<length);
    for(size_t i=at+7;i<=length-3;i++)implicit_source[i]=implicit_source[i+3];
    fragments[1] = compile_sun(GLSLC_STAGE_FRAGMENT,implicit_source);
    char *lod = strstr(sample,",0.0);"); assert(lod);
    length = strlen(implicit_source); at = (size_t)(lod-implicit_source);
    assert(at+4<length);
    for(size_t i=at;i<=length-4;i++)implicit_source[i]=implicit_source[i+4];
    fragments[2] = compile_sun(GLSLC_STAGE_FRAGMENT,implicit_source);
    free(implicit_source);
    /* A two-depth map makes filtered comparison observably different from
     * comparing an interpolated depth, and exercises mixed 25-tap coverage. */
    begin();
    VkClearValue clear = {.depthStencil={.75f,0}};
    VkRenderPassBeginInfo start = {.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass=depth_render,.framebuffer=depth_framebuffer,.renderArea={{0,0},{WIDTH,HEIGHT}},
        .clearValueCount=1,.pClearValues=&clear};
    vkCmdBeginRenderPass(command,&start,VK_SUBPASS_CONTENTS_INLINE);
    VkClearAttachment half = {.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT,.clearValue={.depthStencil={.25f,0}}};
    VkClearRect rect = {.rect={{0,0},{WIDTH/2,HEIGHT}},.layerCount=1};
    vkCmdClearAttachments(command,1,&half,1,&rect); vkCmdEndRenderPass(command);
    VkImageMemoryBarrier barrier = {.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=depth_image,.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier); submit();
    VkDescriptorSetLayoutBinding binding = {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo set_info = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    VkDescriptorSetLayout set_layout; CHECK(vkCreateDescriptorSetLayout(device,&set_info,NULL,&set_layout));
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
    VkDescriptorPoolCreateInfo pool_info = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};
    VkDescriptorPool pool; CHECK(vkCreateDescriptorPool(device,&pool_info,NULL,&pool));
    VkDescriptorSetAllocateInfo allocate_set = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
    VkDescriptorSet set; CHECK(vkAllocateDescriptorSets(device,&allocate_set,&set));
    VkPushConstantRange range = {VK_SHADER_STAGE_FRAGMENT_BIT,0,48};
    VkPipelineLayoutCreateInfo layout_info = {.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&range};
    VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(device,&layout_info,NULL,&layout));
    VkImageCreateInfo image_info = {.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_R32G32B32A32_SFLOAT,.extent={WIDTH,HEIGHT,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VkImage color_image; CHECK(vkCreateImage(device,&image_info,NULL,&color_image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device,color_image,&req);
    VkMemoryAllocateInfo allocate = {.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VkDeviceMemory memory; CHECK(vkAllocateMemory(device,&allocate,NULL,&memory)); CHECK(vkBindImageMemory(device,color_image,memory,0));
    VkImageViewCreateInfo vi = {.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=color_image,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view; CHECK(vkCreateImageView(device,&vi,NULL,&view));
    VkAttachmentDescription attachment = {.format=VK_FORMAT_R32G32B32A32_SFLOAT,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkAttachmentReference color_ref = {0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color_ref};
    VkSubpassDependency dependency = {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,
        .srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
    VkRenderPassCreateInfo render_info = {.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=1,.pDependencies=&dependency};
    VkRenderPass render; CHECK(vkCreateRenderPass(device,&render_info,NULL,&render));
    VkFramebufferCreateInfo framebuffer_info = {.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass=render,.attachmentCount=1,.pAttachments=&view,.width=WIDTH,.height=HEIGHT,.layers=1};
    VkFramebuffer framebuffer; CHECK(vkCreateFramebuffer(device,&framebuffer_info,NULL,&framebuffer));
    VkPipeline pipelines[3];
    for(unsigned i=0;i<3;i++)pipelines[i]=receiver_pipeline(vertex,fragments[i],layout,render);
    Buffer output = make_buffer(WIDTH*HEIGHT*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkFormatProperties format; vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_D32_SFLOAT,&format);
    bool linear_supported = (format.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)!=0;
    unsigned cases=0;size_t pixels=0;
    for(unsigned linear=0;linear<(linear_supported?2u:1u);linear++) {
        VkSamplerCreateInfo sampler_info = {.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter=linear?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=linear?VK_FILTER_LINEAR:VK_FILTER_NEAREST,
            .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .compareEnable=VK_TRUE,.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL,.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE};
        VkSampler sampler; CHECK(vkCreateSampler(device,&sampler_info,NULL,&sampler));
        VkDescriptorImageInfo texture={sampler,depth_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,
            .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&texture};
        vkUpdateDescriptorSets(device,1,&write,0,NULL);
        for(unsigned sampling=0;sampling<3;sampling++)
        for(unsigned x=0;x<8;x++)for(unsigned z=0;z<5;z++)for(unsigned opacity=0;opacity<3;opacity++)for(unsigned w=0;w<3;w++) {
            const float us[]={-.1f,0,.025f,.25f,.49f,.51f,.975f,1.1f};
            const float zs[]={-.1f,.125f,.5f,.875f,1.1f};
            float u=us[x],v=.5f,depth=zs[z],clip_w=w==0?1:(w==1?2:-1);
            float data[12]={.8f,.6f,.4f,.37f,(u*2-1)*clip_w,(v*2-1)*clip_w,depth*clip_w,clip_w,
                .01f,opacity*.5f,1.0f/WIDTH,1.0f/HEIGHT};
            float shade=1;
            if(clip_w>0 && u>0 && u<1 && depth>0 && depth<1) {
                float visibility=0;
                for(int yy=-2;yy<=2;yy++)for(int xx=-2;xx<=2;xx++)visibility+=compare_sample(u+xx*data[10],v+yy*data[11],depth-data[8],linear);
                float edge=fminf(fminf(u,1-u),fminf(v,1-v));
                float coverage=fminf(1,fmaxf(0,edge*10));
                shade=1-data[9]*coverage*(1-visibility/25);
            }
            begin(); VkRenderPassBeginInfo begin_info={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass=render,.framebuffer=framebuffer,.renderArea={{0,0},{WIDTH,HEIGHT}}};
            vkCmdBeginRenderPass(command,&begin_info,VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[sampling]);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);
            vkCmdPushConstants(command,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(data),data);
            vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
            memset(data,0,sizeof(data));
            VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={WIDTH,HEIGHT,1}};
            vkCmdCopyImageToBuffer(command,color_image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output.buffer,1,&copy);
            host_barrier(output,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);submit();
            const float base[]={.8f,.6f,.4f,.37f};
            for(unsigned pixel=0;pixel<WIDTH*HEIGHT;pixel++)for(unsigned c=0;c<4;c++) {
                float expected=base[c]*(c==3?1:shade),actual=((float*)output.mapped)[pixel*4+c];
                if(fabsf(actual-expected)>3e-4f) {fprintf(stderr,"receiver mismatch sample=%u linear=%u u=%g z=%g opacity=%u w=%g c=%u actual=%g expected=%g\n",sampling,linear,u,depth,opacity,clip_w,c,actual,expected);abort();}
            }
            cases++;pixels+=WIDTH*HEIGHT;
        }
        vkDestroySampler(device,sampler,NULL);
    }
    drop_buffer(output);
    for(unsigned i=0;i<3;i++)vkDestroyPipeline(device,pipelines[i],NULL);
    vkDestroyFramebuffer(device,framebuffer,NULL);
    vkDestroyRenderPass(device,render,NULL);vkDestroyImageView(device,view,NULL);vkDestroyImage(device,color_image,NULL);
    vkFreeMemory(device,memory,NULL);vkDestroyPipelineLayout(device,layout,NULL);
    vkDestroyDescriptorPool(device,pool,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);
    for(unsigned i=0;i<3;i++)vkDestroyShaderModule(device,fragments[i],NULL);
    vkDestroyShaderModule(device,vertex,NULL);
    printf("Sun receiver: %u GPU cases, %zu RGBA pixels; 25-tap visibility, coverage/opacity, preserved alpha; linear=%u\n",cases,pixels,linear_supported);
}

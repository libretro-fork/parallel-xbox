/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CPU test, real Vulkan helpers and generated shader precede this fixture. */
int main(void)
{
    gpu_init();assert(validation_enabled);
    VkShaderModule vs=gpu_module(index_vertex_words,sizeof(index_vertex_words)/4);
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT};
    VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    VkDescriptorSetLayout dsl;CHECK(vkCreateDescriptorSetLayout(device,&di,NULL,&dsl));
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&dsl};
    VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&li,NULL,&layout));
    VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&size};
    VkDescriptorPool dp;CHECK(vkCreateDescriptorPool(device,&pi,NULL,&dp));
    VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=dp,.descriptorSetCount=1,.pSetLayouts=&dsl};
    VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&da,&set));
    Buffer output=make_buffer(131072*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),indices=make_buffer(4096,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    VkDescriptorBufferInfo buffer={output.buffer,0,VK_WHOLE_SIZE};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buffer};
    vkUpdateDescriptorSets(device,1,&write,0,NULL);
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};
    VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.subpassCount=1,.pSubpasses=&sub};
    VkRenderPass pass;CHECK(vkCreateRenderPass(device,&rp,NULL,&pass));
    VkFramebufferCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.width=1,.height=1,.layers=1};
    VkFramebuffer fb;CHECK(vkCreateFramebuffer(device,&fc,NULL,&fb));
    VkPipelineShaderStageCreateInfo stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
    VkViewport vp={0,0,1,1,0,1};VkRect2D sc={{0,0},{1,1}};
    VkPipelineViewportStateCreateInfo viewport={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.rasterizerDiscardEnable=VK_TRUE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo samples={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkGraphicsPipelineCreateInfo graphics={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=1,.pStages=&stage,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&viewport,.pRasterizationState=&raster,.pMultisampleState=&samples,.layout=layout,.renderPass=pass};
    VkPipeline pipeline;CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&graphics,NULL,&pipeline));
    const uint32_t idx[]={0,1,255,256,257,65533,65534,2};const unsigned counts[]={0,1,7,8};
    unsigned comparisons=0;
    for(unsigned test=0;test<4;test++)for(unsigned wide=0;wide<2;wide++) {
        unsigned count=counts[test];memset(indices.mapped,0xa5,4096);
        PGRAPHVkState r={.storage_buffers={{indices.mapped,64,4096}}};PGRAPHState pg={&r};
        expected_bytes=((count+1)/2)*4;
        VkDeviceSize packed=pgraph_vk_update_index_buffer_u16(&pg,idx,count);
        memcpy((char *)indices.mapped+512,idx,sizeof(idx));
        memset(output.mapped,0xff,131072*4);begin();
        VkRenderPassBeginInfo bi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{1,1}}};
        vkCmdBeginRenderPass(command,&bi,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);
        vkCmdBindIndexBuffer(command,indices.buffer,wide?512:packed,wide?VK_INDEX_TYPE_UINT32:VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(command,count,1,0,0,0);vkCmdEndRenderPass(command);
        host_barrier(output,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);submit();
        const uint32_t *actual=output.mapped;
        for(unsigned i=0;i<131072;i++) {
            uint32_t expected=UINT32_MAX;
            for(unsigned j=0;j<count;j++)if(idx[j]==i)expected=i^0x12345678;
            assert(actual[i]==expected);
        }
        comparisons++;
    }
    assert(!errors);
    printf("PASS: Vulkan u16/u32 index fetch equivalence: %u validated draws, including empty/odd/even counts and index 65534\n",comparisons);
    vkDestroyPipeline(device,pipeline,NULL);drop_buffer(indices);drop_buffer(output);
    vkDestroyFramebuffer(device,fb,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyDescriptorPool(device,dp,NULL);
    vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,dsl,NULL);vkDestroyShaderModule(device,vs,NULL);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);
    if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");destroy(instance,messenger,NULL);}
    vkDestroyInstance(instance,NULL);
    return 0;
}

/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <volk.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "hw/xbox/nv2a/pgraph/gamma.h"
#include "display-gamma-fixture.h"
/* GPU HELPERS */
typedef struct {VkImage image;VkImageView view;VkDeviceMemory memory;} TestImage;
static TestImage image_new(VkFormat format,unsigned width,unsigned height,VkImageUsageFlags usage)
{
    TestImage result={0};VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=format,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage};
    CHECK(vkCreateImage(device,&ci,NULL,&result.image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,result.image,&req);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    CHECK(vkAllocateMemory(device,&ai,NULL,&result.memory));CHECK(vkBindImageMemory(device,result.image,result.memory,0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=result.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    CHECK(vkCreateImageView(device,&vi,NULL,&result.view));return result;
}
static void image_drop(TestImage image)
{vkDestroyImageView(device,image.view,NULL);vkDestroyImage(device,image.image,NULL);vkFreeMemory(device,image.memory,NULL);}
static void upload(TestImage image,Buffer buffer,unsigned width,unsigned height)
{
    begin();VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={width,height,1}};
    vkCmdCopyBufferToImage(command,buffer.buffer,image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);submit();
}
static GlslcResult compile(const char *path,GlslcStage stage)
{
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);char *text=calloc(n+1,1);assert(text&&fread(text,1,n,f)==(size_t)n);fclose(f);
    GlslcResult result={0};GlslcOptions options={.emit_layout=true};
    if(!glslc_compile(stage,text,&options,&result)){fprintf(stderr,"%s:%d: %s\n",path,result.error_line,result.error);abort();}
    free(text);char name[4096];snprintf(name,sizeof(name),"%s.spv",path);f=fopen(name,"wb");assert(f&&fwrite(result.words,4,result.num_words,f)==result.num_words);fclose(f);return result;
}
static void push_member(uint8_t *push,const GlslcLayout *layout,const char *name,const void *value,size_t bytes)
{
    for(unsigned i=0;i<layout->count;i++)if(!strcmp(layout->members[i].name,name)) {
        assert(layout->members[i].offset+bytes<=layout->size);memcpy(push+layout->members[i].offset,value,bytes);return;
    }
    fprintf(stderr,"missing push member %s\n",name);abort();
}
int main(int argc,char **argv)
{
    assert(argc==4);gpu_init();
    GlslcResult vertex=compile(argv[1],GLSLC_STAGE_VERTEX),fragment[2]={compile(argv[2],GLSLC_STAGE_FRAGMENT),compile(argv[3],GLSLC_STAGE_FRAGMENT)};
    assert(fragment[0].push_constants.size==128&&fragment[1].push_constants.size==128);
    assert(fragment[0].uniforms.size==1024&&fragment[1].uniforms.size==1024);
    VkShaderModule vs=gpu_module(vertex.words,vertex.num_words),fs[2]={gpu_module(fragment[0].words,fragment[0].num_words),gpu_module(fragment[1].words,fragment[1].num_words)};
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);size_t stride=pgraph_gamma_stride(props.limits.minUniformBufferOffsetAlignment,props.limits.nonCoherentAtomSize);
    Buffer staging=make_buffer(GAMMA_WIDTH*GAMMA_HEIGHT*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),output=make_buffer(GAMMA_WIDTH*GAMMA_HEIGHT*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT),lut=make_buffer(stride*PGRAPH_GAMMA_SLOTS,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    TestImage src[3],video=image_new(VK_FORMAT_R8G8B8A8_UNORM,1,1,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT),target=image_new(VK_FORMAT_R8G8B8A8_UNORM,GAMMA_WIDTH,GAMMA_HEIGHT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    const VkFormat formats[]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R5G6B5_UNORM_PACK16,VK_FORMAT_A1R5G5B5_UNORM_PACK16};
    for(unsigned i=0;i<3;i++) {src[i]=image_new(formats[i],GAMMA_WIDTH,GAMMA_HEIGHT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);source_pixels(i,staging.mapped);upload(src[i],staging,GAMMA_WIDTH,GAMMA_HEIGHT);}
    const uint8_t color[]={20,100,210,77};memcpy(staging.mapped,color,4);upload(video,staging,1,1);
    VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.minFilter=VK_FILTER_NEAREST,.magFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT,.addressModeV=VK_SAMPLER_ADDRESS_MODE_REPEAT,.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT};VkSampler sampler;CHECK(vkCreateSampler(device,&si,NULL,&sampler));
    VkDescriptorSetLayoutBinding bindings[2]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT}};
    VkDescriptorSetLayout layouts[2];VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};CHECK(vkCreateDescriptorSetLayout(device,&li,NULL,&layouts[0]));
    VkDescriptorSetLayoutBinding gamma={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};li.bindingCount=1;li.pBindings=&gamma;CHECK(vkCreateDescriptorSetLayout(device,&li,NULL,&layouts[1]));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,PGRAPH_GAMMA_SLOTS}};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1+PGRAPH_GAMMA_SLOTS,.poolSizeCount=2,.pPoolSizes=sizes};VkDescriptorPool descriptors;CHECK(vkCreateDescriptorPool(device,&pi,NULL,&descriptors));
    VkDescriptorSet sets[1+PGRAPH_GAMMA_SLOTS];VkDescriptorSetLayout alloc_layouts[1+PGRAPH_GAMMA_SLOTS];alloc_layouts[0]=layouts[0];for(unsigned i=1;i<1+PGRAPH_GAMMA_SLOTS;i++)alloc_layouts[i]=layouts[1];
    VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptors,.descriptorSetCount=1+PGRAPH_GAMMA_SLOTS,.pSetLayouts=alloc_layouts};CHECK(vkAllocateDescriptorSets(device,&dai,sets));
    for(unsigned i=0;i<PGRAPH_GAMMA_SLOTS;i++) {
        VkDescriptorBufferInfo info={lut.buffer,i*stride,1024};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[i+1],.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&info};vkUpdateDescriptorSets(device,1,&write,0,NULL);
    }
    VkPushConstantRange range={VK_SHADER_STAGE_FRAGMENT_BIT,0,128};VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=2,.pSetLayouts=layouts,.pushConstantRangeCount=1,.pPushConstantRanges=&range};VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&pli,NULL,&layout));
    VkAttachmentDescription attachment={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&reference};VkSubpassDependency dependency={.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
    VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&sub,.dependencyCount=1,.pDependencies=&dependency};VkRenderPass pass;CHECK(vkCreateRenderPass(device,&ri,NULL,&pass));
    VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=1,.pAttachments=&target.view,.width=GAMMA_WIDTH,.height=GAMMA_HEIGHT,.layers=1};VkFramebuffer framebuffer;CHECK(vkCreateFramebuffer(device,&fi,NULL,&framebuffer));
    VkViewport viewport={0,0,GAMMA_WIDTH,GAMMA_HEIGHT,0,1};VkRect2D scissor={{0,0},{GAMMA_WIDTH,GAMMA_HEIGHT}};VkPipeline pipelines[]={pipeline(vs,fs[0],VK_NULL_HANDLE,layout,pass,viewport,scissor),pipeline(vs,fs[1],VK_NULL_HANDLE,layout,pass,viewport,scissor)};
    unsigned cases=0;uint8_t baseline[GAMMA_WIDTH*GAMMA_HEIGHT*4];
    for(unsigned variant=0;variant<2;variant++)for(unsigned format=0;format<3;format++) {
        VkDescriptorImageInfo images[]={{sampler,src[format].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,video.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[2];for(unsigned i=0;i<2;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[0],.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&images[i]};vkUpdateDescriptorSets(device,2,writes,0,NULL);
        for(unsigned curve=0;curve<5;curve++)for(unsigned overlay=0;overlay<3;overlay++)for(unsigned enabled=0;enabled<2;enabled++) {
            unsigned slot=cases%PGRAPH_GAMMA_SLOTS;gamma_table((uint32_t*)((uint8_t*)lut.mapped+slot*stride),curve);
            uint8_t push[128]={0};float one=1,size[]={GAMMA_WIDTH,GAMMA_HEIGHT},pos[]={0,0,GAMMA_WIDTH,GAMMA_HEIGHT},scale[]={1,1,1,(float)enabled};uint32_t video_enable=overlay!=0,key=overlay==2;
            const GlslcLayout *pc=&fragment[variant].push_constants;
            push_member(push,pc,"line_offset",&one,4);push_member(push,pc,"display_size",size,8);push_member(push,pc,"pvideo_enable",&video_enable,4);push_member(push,pc,"pvideo_pos",pos,16);push_member(push,pc,"pvideo_scale",scale,16);push_member(push,pc,"pvideo_color_key_enable",&key,4);
            if(variant) {
                float transform[]={GAMMA_WIDTH*(format==0?4:2),0,1,-1};uint32_t bytes[]={0,GAMMA_WIDTH*(format==0?4:2),0,(format+1)|((format+1)<<8)};
                push_member(push,pc,"source_transform",transform,16);push_member(push,pc,"source_bytes",bytes,16);
            }
            begin();VkRenderPassBeginInfo bi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea={{0,0},{GAMMA_WIDTH,GAMMA_HEIGHT}}};vkCmdBeginRenderPass(command,&bi,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[variant]);
            VkDescriptorSet bound[]={sets[0],sets[slot+1]};vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,2,bound,0,NULL);vkCmdPushConstants(command,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,128,push);vkCmdDraw(command,3,1,0,0);vkCmdEndRenderPass(command);
            VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={GAMMA_WIDTH,GAMMA_HEIGHT,1}};vkCmdCopyImageToBuffer(command,target.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output.buffer,1,&copy);host_barrier(output,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);submit();
            if(!enabled)memcpy(baseline,output.mapped,sizeof(baseline));
            gamma_check(output.mapped,format,curve,overlay,enabled,false,format?baseline:NULL);cases++;
        }
    }
    printf("PASS: %u actual Vulkan native/byte display cases, %u RGBA components; 128-byte push ABI, 1024-byte LUT, slot descriptors, alpha and overlay ordering\n",cases,cases*GAMMA_WIDTH*GAMMA_HEIGHT*4);
    for(unsigned i=0;i<2;i++){vkDestroyPipeline(device,pipelines[i],NULL);vkDestroyShaderModule(device,fs[i],NULL);free(fragment[i].words);free(fragment[i].uniforms.members);free(fragment[i].push_constants.members);}
    vkDestroyShaderModule(device,vs,NULL);free(vertex.words);free(vertex.uniforms.members);free(vertex.push_constants.members);
    vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorPool(device,descriptors,NULL);for(unsigned i=0;i<2;i++)vkDestroyDescriptorSetLayout(device,layouts[i],NULL);vkDestroySampler(device,sampler,NULL);
    for(unsigned i=0;i<3;i++)image_drop(src[i]);image_drop(video);image_drop(target);drop_buffer(lut);drop_buffer(output);drop_buffer(staging);
    vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);
    if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");destroy(instance,messenger,NULL);}vkDestroyInstance(instance,NULL);assert(!errors);
}

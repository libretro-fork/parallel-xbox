/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/spv/psh.h"
#define CHECK(x) do { VkResult result=(x); if(result) { fprintf(stderr,"Vulkan %d at %d\n",result,__LINE__);abort(); } } while(0)
/* Test-only attribution; never linked into the core. Raw wall times are
 * not isolated GPU/driver-cold qualification or presentation measurements. */
static uint64_t wall_ns(void){
#ifdef _WIN32
 LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);
 return (uint64_t)((long double)t.QuadPart*1000000000.0L/f.QuadPart);
#else
 struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;
#endif
}
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static unsigned errors;
typedef struct { VkBuffer handle;VkDeviceMemory memory;uint8_t *mapped; } Buffer;
static Buffer buffer(VkDeviceSize size,VkBufferUsageFlags usage) {
 Buffer b={0};VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
 CHECK(vkCreateBuffer(device,&ci,NULL,&b.handle));VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b.handle,&req);
 unsigned type=0;for(;type<memory_properties.memoryTypeCount;type++) if((req.memoryTypeBits&(1u<<type)) &&
 (memory_properties.memoryTypes[type].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))break;
 assert(type<memory_properties.memoryTypeCount);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
 CHECK(vkAllocateMemory(device,&ai,NULL,&b.memory));CHECK(vkBindBufferMemory(device,b.handle,b.memory,0));
 CHECK(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,(void **)&b.mapped));return b;
}
static void drop(Buffer b) {vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.handle,NULL);vkFreeMemory(device,b.memory,NULL);}
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user) {
 if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){errors++;fprintf(stderr,"%s\n",data->pMessage);}return VK_FALSE;
}

typedef struct {VkImage image;VkImageView view;VkDeviceMemory memory;} Image;
static Image image_shape(VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect,unsigned levels,unsigned kind){
 Image a={0};VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.flags=kind==2?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0,.imageType=kind==1?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D,.format=format,.extent={4,4,kind==1?4:1},.mipLevels=levels,.arrayLayers=kind==2?6:1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage};
 CHECK(vkCreateImage(device,&ci,NULL,&a.image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,a.image,&req);unsigned type=0;while(!(req.memoryTypeBits&(1u<<type)))type++;assert(type<memory_properties.memoryTypeCount);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};CHECK(vkAllocateMemory(device,&ai,NULL,&a.memory));CHECK(vkBindImageMemory(device,a.image,a.memory,0));
 VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=a.image,.viewType=kind==1?VK_IMAGE_VIEW_TYPE_3D:kind==2?VK_IMAGE_VIEW_TYPE_CUBE:VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={aspect,0,levels,0,kind==2?6:1}};CHECK(vkCreateImageView(device,&vi,NULL,&a.view));return a;
}
static Image image_levels(VkFormat f,VkImageUsageFlags u,VkImageAspectFlags a,unsigned levels){return image_shape(f,u,a,levels,0);}
static Image image(VkFormat f,VkImageUsageFlags u,VkImageAspectFlags a){return image_levels(f,u,a,1);}
static void drop_image(Image a){vkDestroyImageView(device,a.view,NULL);vkDestroyImage(device,a.image,NULL);vkFreeMemory(device,a.memory,NULL);}
static VkShaderModule module(const GlslcResult *a){VkShaderModule m;VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=a->num_words*4,.pCode=a->words};CHECK(vkCreateShaderModule(device,&ci,NULL,&m));return m;}
static void release(GlslcResult *a){glslc_result_free_words(a);free(a->uniforms.members);free(a->push_constants.members);}
static void pack(uint8_t *dst,const GlslcLayout *layout,const PshUniformValues *values){
 for(size_t j=0;j<layout->count;j++){const GlslcUniform *u=layout->members+j;unsigned k=0;while(k<PshUniform__COUNT && strcmp(PshUniformInfo[k].name,u->name))k++;assert(k<PshUniform__COUNT);const uint8_t *src=(const uint8_t *)values+PshUniformInfo[k].val_offs;for(size_t n=0;n<u->dim_a;n++)memcpy(dst+u->offset+n*u->stride,src+n*u->dim_v*4,u->dim_v*4);}
}
static VkShaderModule vertex(bool smooth,bool dimensional){
 MString *s=mstring_from_str("#version 450\n");pgraph_glsl_get_vtx_header(s,true,smooth,false,false,false);
 mstring_append(s,"layout(push_constant) uniform Vertex {vec4 d0;vec4 d1;vec4 b0;vec4 b1;vec4 p0;vec4 p1;vec4 p2;vec4 extra;} v;\nvoid main(){int i=gl_VertexIndex;if(v.extra.z<0.0 && i!=0)i=3-i;vec2 p=i==0?vec2(-1.0):i==1?vec2(3.0,-1.0):vec2(-1.0,3.0);gl_Position=v.extra.w<0.0?vec4(0.0,0.0,0.5,1.0):vec4(p,0.5,1.0);gl_PointSize=v.extra.w<0.0?4.0:1.0;vtxD0=v.d0+float(i)*vec4(0.125,-0.0625,0.25,0.0625);vtxD1=v.d1+float(i)*vec4(-0.125,0.25,0.0625,-0.0625);vtxB0=v.b0+float(i)*vec4(0.0625,0.125,-0.125,0.0625);vtxB1=v.b1+float(i)*vec4(-0.25,0.0625,0.125,-0.0625);vtxFog=v.extra.x+float(i)*0.125;vtxPos0=v.p0;vtxPos1=v.p1;vtxPos2=v.p2;triMZ=v.extra.y;vtxT0=vec4(0.0);vtxT1=vec4(0.0);vtxT2=vec4(0.0);vtxT3=vec4(0.0);if(v.extra.w!=0.0){int k=int(abs(v.extra.w))-1;float q=(k%8)==0?1.0:(k%8)==1?0.5:(k%8)==2?-0.5:(k%8)==3?0.0:(k%8)==4?1e-20:(k%8)==5?2.0:(k%8)==6?1.25:3.0;vec2 uv=(p+vec2(1.0))*0.5;float m=float((k/8)%4+1);vtxT0=vec4(uv*m-vec2(0.25),0.0,q);vtxT1=vec4(uv.yx*m+vec2(0.125),0.25,q);vtxT2=vec4(-uv*m+vec2(0.75),-0.25,q);vtxT3=vec4(uv*m-vec2(0.5),0.5,q); }\n");
 if(dimensional)mstring_append(s,"int k=int(abs(v.extra.w))-128;int style=k%16;vec2 uv=(p+vec2(1.0))*0.5;vec2 m=(uv*2.0-vec2(1.0))*0.25;vec3 d; if(style==0)d=vec3(1.0,m.x,m.y);else if(style==1)d=vec3(-1.0,m.x,m.y);else if(style==2)d=vec3(m.x,1.0,m.y);else if(style==3)d=vec3(m.x,-1.0,m.y);else if(style==4)d=vec3(m.x,m.y,1.0);else if(style==5)d=vec3(m.x,m.y,-1.0);else if(style==6)d=vec3(1.0,1.0,0.0);else if(style==7)d=vec3(0.0,1.0,1.0);else if(style==8)d=vec3(1.0,0.0,1.0);else if(style==9)d=vec3(0.0);else if(style==10)d=vec3(1.0);else if(style==11)d=vec3(-0.0,0.0,0.0);else d=vec3(uv*1.5+vec2(0.125),float(k%4)*0.25+uv.x*0.125);int qi=(k/16)%8;float q=qi==0?1.0:qi==1?0.5:qi==2?2.0:qi==3?-0.5:qi==4?0.0:qi==5?1e-20:qi==6?1.25:3.0;vtxT0=vec4(d,q);vtxT1=vec4(d.yzx,q);vtxT2=vec4(-d,q);vtxT3=vec4(d.zxy,q);\n");
 mstring_append(s,"}\n");
 GlslcResult r;if(!glslc_compile(GLSLC_STAGE_VERTEX,mstring_get_str(s),NULL,&r)){fprintf(stderr,"vertex %d %s\n",r.error_line,r.error);abort();}VkShaderModule m=module(&r);release(&r);mstring_unref(s);return m;
}
static Image dimensional_texture(unsigned kind,VkCommandBuffer cmd,VkQueue queue,VkFence fence){
 Image a=image_shape(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,3,kind);
 Buffer staging=buffer(kind==1?1168:2016,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);float *data=(float *)staging.mapped;VkBufferImageCopy levels[3];unsigned texel=0;
 for(unsigned level=0;level<3;level++){
  unsigned side=4u>>level,depth=kind==1?side:1,layers=kind==2?6:1;
  levels[level]=(VkBufferImageCopy){.bufferOffset=texel*16,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,layers},.imageExtent={side,side,depth}};
  for(unsigned face=0;face<layers;face++)for(unsigned z=0;z<depth;z++)for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++,texel++){
   data[texel*4]=kind==2?(face+1)*.125f:(x+1)*.125f;data[texel*4+1]=(y+level)*.125f;data[texel*4+2]=kind==2?(x+1)*.125f:(z+level+1)*.125f;data[texel*4+3]=(x+y+z+face+level)%3?.5f:0;
  }
 }
 assert(texel*16==(kind==1?1168:2016));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
 VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=a.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,kind==2?6:1}};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);vkCmdCopyBufferToImage(cmd,staging.handle,a.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,3,levels);
 b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&b);CHECK(vkEndCommandBuffer(cmd));
 VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));CHECK(vkResetCommandBuffer(cmd,0));drop(staging);return a;
}
static unsigned fixture_kind(const PshState *s,unsigned j){unsigned mode=(s->shader_stage_program>>(j*5))&31;return mode==PS_TEXTUREMODES_CUBEMAP?(s->tex_cubemap[j]?2:0):s->dim_tex[j]==3?1:mode==PS_TEXTUREMODES_PROJECT2D && s->tex_cubemap[j]?2:0;}
static void fixture_family(PshState *s,unsigned unit,unsigned family){
 unsigned mode=family<1?PS_TEXTUREMODES_PROJECT2D:family<3?PS_TEXTUREMODES_PROJECT3D:family==3?PS_TEXTUREMODES_PROJECT2D:PS_TEXTUREMODES_CUBEMAP;
 s->shader_stage_program=(s->shader_stage_program&~(31u<<(unit*5)))|(mode<<(unit*5));s->dim_tex[unit]=family<2?3:2;s->tex_cubemap[unit]=family==3 || family==4;
}
static uint32_t encoded_inputs(unsigned seed,bool alpha,bool ef){
 const unsigned regs[]={0,1,2,3,4,5,8,9,10,11,12,13,14,15};uint32_t v=0;
 for(unsigned j=0;j<4;j++){unsigned r=regs[(seed+j*5)%(ef?14:13)],mapping=alpha?((seed+j)&1?0x20:0):((seed+j*3)&7)<<5,channel=alpha?16:((seed>>j)&1?16:0);v=(v<<8)|r|mapping|channel;}return v;
}
static uint32_t dynamic_inputs(unsigned seed,bool alpha,bool signed_alpha){
 uint32_t v=0;for(unsigned j=0;j<4;j++){
  unsigned reg=(seed+j*5)&15,mapping=alpha && !signed_alpha?((seed+j)&1):((seed+j*3)&7);
  v=(v<<8)|reg|(mapping<<5)|(alpha || ((seed>>j)&1)?16:0);
 }return v;
}
int main(int argc,char **argv){
 unsigned first=argc==3?strtoul(argv[1],NULL,10):0,limit=argc==3?strtoul(argv[2],NULL,10):64;assert(first<limit);printf("PSH fixtures [%u,%u)\n",first,limit);
 bool validated=getenv("NO_VALIDATION")==NULL;const char *layer="VK_LAYER_KHRONOS_validation",*debug=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledLayerCount=validated,.ppEnabledLayerNames=&layer,.enabledExtensionCount=validated,.ppEnabledExtensionNames=&debug};VkInstance instance;CHECK(vkCreateInstance(&ici,NULL,&instance));
 PFN_vkCreateDebugUtilsMessengerEXT create=(void *)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void *)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
 VkDebugUtilsMessengerCreateInfoEXT mi={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=7,.pfnUserCallback=validation};if(validated)CHECK(create(instance,&mi,NULL,&messenger));
 uint32_t n=1;VkPhysicalDevice gpu;CHECK(vkEnumeratePhysicalDevices(instance,&n,&gpu));VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(gpu,&props);puts(props.deviceName);vkGetPhysicalDeviceMemoryProperties(gpu,&memory_properties);
 VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f13};vkGetPhysicalDeviceFeatures2(gpu,&features);if(!features.features.largePoints){puts("UNQUALIFIED: large points unavailable");return 2;}if(!f13.shaderDemoteToHelperInvocation){puts("UNQUALIFIED: shader demote unavailable");return 2;}
 uint32_t qn=32;VkQueueFamilyProperties families[32];vkGetPhysicalDeviceQueueFamilyProperties(gpu,&qn,families);unsigned family=0;while(family<qn && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<qn);
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};f13=(VkPhysicalDeviceVulkan13Features){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.shaderDemoteToHelperInvocation=VK_TRUE};VkPhysicalDeviceFeatures enabled={.largePoints=VK_TRUE};VkDeviceCreateInfo dci={.pEnabledFeatures=&enabled,.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f13,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};CHECK(vkCreateDevice(gpu,&dci,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
 Buffer uniform=buffer(2048,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),readback=buffer(320,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
 Image color=image(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT),depth=image(VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
 VkAttachmentDescription attachments[2]={{.format=VK_FORMAT_R32G32B32A32_SFLOAT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},{.format=VK_FORMAT_D32_SFLOAT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}};
 VkAttachmentReference cr={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},dr={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&cr,.pDepthStencilAttachment=&dr};
 VkSubpassDependency deps[2]={{.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},{.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT}};
 VkRenderPassCreateInfo rci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=2,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&sub,.dependencyCount=2,.pDependencies=deps};VkRenderPass pass;CHECK(vkCreateRenderPass(device,&rci,NULL,&pass));VkImageView views[]={color.view,depth.view};VkFramebufferCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=2,.pAttachments=views,.width=4,.height=4,.layers=1};VkFramebuffer fb;CHECK(vkCreateFramebuffer(device,&fci,NULL,&fb));
 VkDescriptorSetLayoutBinding binding[5]={{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT}};for(unsigned j=0;j<4;j++)binding[j+1]=(VkDescriptorSetLayoutBinding){.binding=2+j,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};VkDescriptorSetLayoutCreateInfo lci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=5,.pBindings=binding};VkDescriptorSetLayout set_layout;CHECK(vkCreateDescriptorSetLayout(device,&lci,NULL,&set_layout));VkPushConstantRange push={VK_SHADER_STAGE_VERTEX_BIT,0,128};VkPipelineLayoutCreateInfo plci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&plci,NULL,&layout));
 VkDescriptorPoolSize pool_size[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4}};VkDescriptorPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=pool_size};VkDescriptorPool pool;CHECK(vkCreateDescriptorPool(device,&pci,NULL,&pool));VkDescriptorSetAllocateInfo sai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&sai,&set));VkDescriptorBufferInfo bi={uniform.handle,0,2048};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&bi};vkUpdateDescriptorSets(device,1,&write,0,NULL);
 VkCommandPoolCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool commands;CHECK(vkCreateCommandPool(device,&cpci,NULL,&commands));VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cmd;CHECK(vkAllocateCommandBuffers(device,&cai,&cmd));VkFenceCreateInfo fence_ci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fence_ci,NULL,&fence));
 Image texture=image_levels(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,3);
 Buffer upload=buffer(336,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);float *pixels=(float *)upload.mapped;unsigned texel=0;VkBufferImageCopy levels[3]={0};
 for(unsigned level=0;level<3;level++){
  unsigned side=4u>>level;levels[level]=(VkBufferImageCopy){.bufferOffset=texel*16,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={side,side,1}};
  for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++,texel++){
   pixels[texel*4]=(x+level+1)*.125f;pixels[texel*4+1]=(y+level)*.25f;pixels[texel*4+2]=((x^y)+level)*.125f;pixels[texel*4+3]=(x+y+level)%3?.5f:0;
  }
 }
 VkCommandBufferBeginInfo upload_begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&upload_begin));
 VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=texture.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1}};
 vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);vkCmdCopyBufferToImage(cmd,upload.handle,texture.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,3,levels);
 barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);CHECK(vkEndCommandBuffer(cmd));
 VkSubmitInfo upload_submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&upload_submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));CHECK(vkResetCommandBuffer(cmd,0));drop(upload);
 VkSampler samplers[16];
 for(unsigned j=0;j<16;j++){
  VkSamplerCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=(j&1)?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=(j&1)?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.mipmapMode=(j&2)?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=(VkSamplerAddressMode)(j>>2),.addressModeV=(VkSamplerAddressMode)(j>>2),.addressModeW=(VkSamplerAddressMode)(j>>2),.maxLod=2,.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK};CHECK(vkCreateSampler(device,&sci,NULL,&samplers[j]));
 }
 Image volume=dimensional_texture(1,cmd,queue,fence),cube=dimensional_texture(2,cmd,queue,fence);
 VkShaderModule vertices[2][2]={{vertex(false,false),vertex(true,false)},{vertex(false,true),vertex(true,true)}};unsigned failures=0,compared=0,written=0,discarded=0,sample_pixels=0,point_pixels=0;uint64_t words[2]={0},reference_source_bytes=0;bool timings=getenv("PSH_COMPILER_TIMINGS")!=NULL;
 for(unsigned test=first;test<limit;test++){
  PshState state={.final_inputs_0=0x04200000,.final_inputs_1=0x1400};state.smooth_shading=!!(test&1);state.z_perspective=!!(test&2);state.depth_clipping=!!(test&4);state.window_clip_exclusive=!!(test&8);state.alpha_test=!!(test&16);state.alpha_dynamic=!!(test&32);state.depth_format=(test/64)%4;
  if(test>=512){
   unsigned f=test-512;state.combiner_control=(f%9) | (((f&1?PS_COMBINERCOUNT_MUX_MSB:0)|(f&2?PS_COMBINERCOUNT_UNIQUE_C0:0)|(f&4?PS_COMBINERCOUNT_UNIQUE_C1:0))<<8);
   for(unsigned j=0;j<(state.combiner_control&255);j++){
    state.rgb_inputs[j]=encoded_inputs(f+j,false,false);state.alpha_inputs[j]=encoded_inputs(f+j+5,true,false);
    state.rgb_outputs[j]=(((f+j)&255)<<12) | (12<<8) | (4<<4) | 5;
    state.alpha_outputs[j]=((f+j)&4)<<12 | (13<<8) | (12<<4) | 5;
   }
   if(f&8){state.final_inputs_0=encoded_inputs(f+7,false,true);uint32_t e=encoded_inputs(f+11,false,false),g=encoded_inputs(f+3,true,true);state.final_inputs_1=(e&0xffff0000u)|((g>>16)&0xff00u)|((f&7)<<5);}
  }
  PshUniformValues values={0};values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.clipRange[0][1]=state.depth_format==DEPTH_FORMAT_D16?65535:16777215;values.clipRange[0][2]=0;values.clipRange[0][3]=values.clipRange[0][1];values.alphaFunc[0]=test%8;values.alphaRef[0]=128;values.alphaTestEnable[0]=test&64;values.twoSideSel[0]=!!(test&128);
  values.fogColor[0][0]=.125f;values.fogColor[0][1]=.25f;values.fogColor[0][2]=.75f;
  for(unsigned j=0;j<18;j++)for(unsigned k=0;k<4;k++)values.consts[j][k]=k==3?.25f+(j%3)*.25f:((int)((j*7+k*3+test)%17)-8)*.125f;
  for(unsigned j=0;j<8;j++){values.clipRegion[j][0]=test&8?10:0;values.clipRegion[j][1]=0;values.clipRegion[j][2]=test&8?11:4;values.clipRegion[j][3]=4;}
  float data[8][4]={{.125f,.25f,.5f,.75f},{.5f,.25f,.125f,.25f},{.75f,.5f,.25f,.125f},{.25f,.5f,.75f,.875f},{0,0,100,1},{4,0,200,1},{0,4,300,1},{.5f,0,1,0}};data[7][2]=test&256?-1:1;
  if(test>=2048){
   unsigned f=test-2048,shape=(f/16)%8;
   values.surfaceScale[0][0]=1+(f%3);values.surfaceScale[0][1]=1+((f/3)%3);
   values.depthOffset[0]=(f&1)?.125f:-.25f;values.depthFactor[0]=(f&2)?.125f:-.0625f;data[7][1]=(f&4)?.5f:-.25f;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][0]=1;values.clipRegion[j][1]=1;values.clipRegion[j][2]=3;values.clipRegion[j][3]=3;}
   if(shape==0){data[4][0]=.125f;data[4][1]=.25f;data[5][0]=5.5f;data[5][1]=1.125f;data[6][0]=.375f;data[6][1]=6.25f;}
   if(shape==1){data[5][0]=1e30f;data[6][1]=1e30f;}
   if(shape==2){data[5][0]=0;data[5][1]=0;data[6][0]=0;data[6][1]=0;}
   if(shape==3){data[5][0]=INFINITY;data[6][1]=INFINITY;}
   if(shape==4){data[4][3]=0;data[5][3]=0;data[6][3]=0;}
   if(shape==5){data[4][3]=-.5f;data[5][3]=-1.25f;data[6][3]=-2;}
   if(shape==6){data[4][3]=.25f;data[5][3]=1.25f;data[6][3]=2.5f;values.clipRange[0][2]=100;values.clipRange[0][3]=250;}
   if(shape==7){data[4][0]=1e20f;data[4][1]=-1e20f;data[5][0]=2e20f;data[5][1]=1e20f;data[6][0]=-1e20f;data[6][1]=2e20f;}
  }
  if(test>=4096){
   unsigned f=(test-4096)%4096;const unsigned modes[]={PS_TEXTUREMODES_NONE,PS_TEXTUREMODES_PROJECT2D,PS_TEXTUREMODES_PASSTHRU,PS_TEXTUREMODES_CLIPPLANE};
   state.shader_stage_program=0;state.combiner_control=0;state.depth_clipping=false;state.window_clip_exclusive=false;state.z_perspective=false;
   for(unsigned j=0;j<4;j++){
    state.shader_stage_program|=modes[(f>>(j*2))&3]<<(j*5);state.dim_tex[j]=2;state.rect_tex[j]=!!(f&(1u<<(j+7)));state.alphakill[j]=!!(f&2048);
    for(unsigned k=0;k<4;k++)state.compare_mode[j][k]=!!(f&(1u<<(k+4)));
    values.texScale[j]=.5f+(j%3)*.5f;
    VkDescriptorImageInfo info={samplers[(f/256+j)%16],texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   state.combiner_control=((f/64)%9) | ((PS_COMBINERCOUNT_MUX_MSB|PS_COMBINERCOUNT_UNIQUE_C0|PS_COMBINERCOUNT_UNIQUE_C1)<<8);
   for(unsigned j=0;j<(state.combiner_control&255);j++){
    state.rgb_inputs[j]=encoded_inputs(f+j,false,false);state.alpha_inputs[j]=encoded_inputs(f+j+5,true,false);
    state.rgb_outputs[j]=(((f+j)&255)<<12)|(12<<8)|(4<<4)|5;state.alpha_outputs[j]=((f+j)&4)<<12|(13<<8)|(12<<4)|5;
   }
   unsigned reg=PS_REGISTER_T0+((f>>8)&3);if((f&1024) && (state.combiner_control&255))reg=PS_REGISTER_R0;state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;
   values.alphaKillMask[0]=(f/16)%16;values.alphaTestEnable[0]=f&1024;values.alphaFunc[0]=f%8;values.clipRange[0][2]=0;values.clipRange[0][3]=values.clipRange[0][1];values.depthOffset[0]=values.depthFactor[0]=0;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][0]=0;values.clipRegion[j][1]=0;values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;data[7][3]=(float)(f%32+1);if(test>=8192){state.point_sprite=true;state.rect_tex[3]=false;data[7][3]=-data[7][3];state.shader_stage_program=(state.shader_stage_program&~(31u<<15))|((f&1?PS_TEXTUREMODES_PROJECT2D:PS_TEXTUREMODES_PASSTHRU)<<15);state.final_inputs_0=0x200b0000;state.final_inputs_1=0x1b00;}values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;
  }
  if(test>=10240){
   unsigned f=test-10240;state.combiner_dynamic=true;
   if(test<14336){state.shader_stage_program=0;state.point_sprite=false;data[7][3]=0;}
   values.combCtl[0]=(f%9)|(((f&1 || f&8)?0x100:0)|(f&2?0x1000:0)|(f&4?0x10000:0));
   values.combFinal0[0]=dynamic_inputs(f+7,false,true);
   uint32_t ef=dynamic_inputs(f+11,false,true),g=dynamic_inputs(f+3,true,true);
   values.combFinal1[0]=(ef&0xffff0000u)|((g>>16)&0xff00u)|((f&7)<<5);
   if(f%17==0)values.combFinal0[0]=values.combFinal1[0]=0;
   for(unsigned j=0;j<8;j++){
    values.combRgbIn[j]=dynamic_inputs(f+j,false,true);values.combAlphaIn[j]=dynamic_inputs(f+j+5,true,!!(f&8));
    values.combRgbOut[j]=(((f+j)&255)<<12)|(((f+j*3)&15)<<8)|(((f+j)&15)<<4)|((f+j*5)&15);
    values.combAlphaOut[j]=((f+j)&4)<<12|((f&8?((f+j)&7):0)<<15)|(13<<8)|(12<<4)|((f+j)&15);
   }
   /* Keep the LSB float-to-uint conversion defined, including stage zero. */
   if(test>=14336)values.combCtl[0]|=0x100;
   if(test>=16384){
    static const float special[]={0.0f,-0.0f,0x1p-126f,0x1p-149f,1.00000011920928955078125f,-1.00000011920928955078125f,1e20f,-1e20f,INFINITY,-INFINITY,NAN,.1f,-.1f,.33333334f,-.75f,1.75f};
    state.shader_stage_program=0;state.point_sprite=false;data[7][3]=0;state.alpha_dynamic=true;values.alphaTestEnable[0]=0;values.combCtl[0]|=0x100;
    for(unsigned j=0;j<18;j++)for(unsigned k=0;k<4;k++)values.consts[j][k]=k==3?.5f:special[(f+j*3+k)%16];
    for(unsigned j=0;j<4;j++)for(unsigned k=0;k<3;k++)data[j][k]=special[(f+j*5+k+7)%16];
   }
   state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;
   memset(state.rgb_inputs,0xa5,sizeof(state.rgb_inputs));memset(state.alpha_inputs,0x5a,sizeof(state.alpha_inputs));
  }
  if(test>=16896){
   unsigned f=test-16896,n=f,unit=n%4;n/=4;bool rect=n%2;n/=2;bool border=n%2;n/=2;unsigned key=n%4;n/=4;unsigned kernel=n%3;n/=3;unsigned alpha=n%4;n/=4;bool live=n%2;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.smooth_shading=!!(f&1);state.combiner_dynamic=live;state.alpha_dynamic=!!(alpha&1);state.depth_format=DEPTH_FORMAT_D24;
   state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D<<(unit*5);state.dim_tex[unit]=2;state.rect_tex[unit]=rect;state.border_adjust[unit]=border;state.colorkey_mode[unit]=key;
   state.conv_tex[unit]=kernel==0?CONVOLUTION_FILTER_DISABLED:kernel==1?CONVOLUTION_FILTER_GAUSSIAN:CONVOLUTION_FILTER_QUINCUNX;state.alphakill[unit]=!!(alpha&2);
   state.point_sprite=unit==3 && !rect && !!(f&128);data[7][3]=(f&1)?2:6;if(state.point_sprite)data[7][3]=-data[7][3];
   values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.alphaKillMask[0]=1u<<unit;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.colorKey[j]=(f&32)?0x80200000u:0x00200000u;
    const uint32_t masks[]={0,0xff000000u,0x00ffffffu,0xffffffffu};values.colorKeyMask[j]=masks[(f/64+j)%4];
    for(unsigned k=0;k<3;k++){values.borderLogicalSize[j][k]=4.0f+j*.5f;values.borderInvRealSize[j][k]=1.0f/(12+j*2);}
    VkDescriptorImageInfo info={samplers[(f/16+j)%16],texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   uint32_t reg=PS_REGISTER_T0+unit;unsigned stages=(f/64)%9;
   state.combiner_control=stages;values.combCtl[0]=stages|0x100;
   for(unsigned j=0;j<8;j++){
    state.rgb_inputs[j]=(reg<<24)|0x00200000u;state.alpha_inputs[j]=((reg|16u)<<24)|0x00200000u;
    state.rgb_outputs[j]=12<<4;state.alpha_outputs[j]=12<<4;
    values.combRgbIn[j]=state.rgb_inputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
   }
   if(stages)reg=PS_REGISTER_R0;
   state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;
   if(f>=1536){
    state.combiner_dynamic=!!(f&2);state.alpha_dynamic=!!(f&1);state.point_sprite=!!(f&4);data[7][3]=state.point_sprite?-2:2;
    state.shader_stage_program=0;state.combiner_control=2;values.combCtl[0]=2|0x100;
    for(unsigned j=0;j<4;j++){
     state.shader_stage_program|=PS_TEXTUREMODES_PROJECT2D<<(j*5);state.dim_tex[j]=2;state.rect_tex[j]=!!(f&(1u<<j)) && !(j==3 && state.point_sprite);state.border_adjust[j]=!!(f&(1u<<(j+4)));state.colorkey_mode[j]=(f+j)%4;state.conv_tex[j]=((f+j)%3)==0?CONVOLUTION_FILTER_DISABLED:((f+j)%3)==1?CONVOLUTION_FILTER_GAUSSIAN:CONVOLUTION_FILTER_QUINCUNX;
    }
    for(unsigned j=0;j<2;j++){
     state.rgb_inputs[j]=((8+j*2)<<24)|((9+j*2)<<16);state.rgb_outputs[j]=(12+j)<<4;state.alpha_inputs[j]=0x18200000;state.alpha_outputs[j]=12<<4;
     values.combRgbIn[j]=state.rgb_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
    }
    state.final_inputs_0=0x200c000d;state.final_inputs_1=0x1c00;
   }
   values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;
  }
  if(test>=18944){
   unsigned f=test-18944,unit=f%4,n=f/4,family=n%6;n/=6;bool live=n%2;n/=2;unsigned coordinate=n%64;
   memset(&state,0,sizeof(state));memset(&values,0,sizeof(values));state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=!!(f&1);state.combiner_dynamic=live;state.alpha_dynamic=!!(f&256);
   fixture_family(&state,unit,family);state.rect_tex[unit]=state.dim_tex[unit]==2 && family!=4 && family!=5 && !!(f&64);state.border_adjust[unit]=!!(f&128);state.colorkey_mode[unit]=(f/256)%4;state.alphakill[unit]=!!(f&512);
   state.point_sprite=unit==3 && !state.rect_tex[3] && !!(f&1024);data[7][3]=(float)(128+coordinate);if(state.point_sprite)data[7][3]=-data[7][3];
   uint32_t reg=PS_REGISTER_T0+unit;state.final_inputs_0=0x20000000u|(reg<<16);state.final_inputs_1=(reg|16u)<<8;
   if(f>=3072){
    state.shader_stage_program=0;state.point_sprite=!!(f&4);data[7][3]=(float)(128+(f%128));if(state.point_sprite)data[7][3]=-data[7][3];
    for(unsigned j=0;j<4;j++){
     unsigned choice=(f&256)?(f/4)%6:(f/4+j*7)%6;fixture_family(&state,j,choice);state.rect_tex[j]=state.dim_tex[j]==2 && choice==2 && !!(f&(1u<<j)) && !(j==3 && state.point_sprite);state.border_adjust[j]=!!(f&(1u<<(j+4)));state.colorkey_mode[j]=(f&256)?COLOR_KEY_NONE:(int)((f+j)%4);if(f&256)state.alphakill[j]=false;
    }
    if(f&256)state.alpha_dynamic=false;
    state.combiner_control=2;values.combCtl[0]=2|0x100;
    for(unsigned j=0;j<2;j++){
     state.rgb_inputs[j]=((8+j*2)<<24)|((9+j*2)<<16);state.rgb_outputs[j]=(12+j)<<4;state.alpha_inputs[j]=0x18200000;state.alpha_outputs[j]=12<<4;
     values.combRgbIn[j]=state.rgb_inputs[j];values.combRgbOut[j]=state.rgb_outputs[j];values.combAlphaIn[j]=state.alpha_inputs[j];values.combAlphaOut[j]=state.alpha_outputs[j];
    }
    state.final_inputs_0=0x200c000d;state.final_inputs_1=0x1c00;
   }
   values.combFinal0[0]=state.final_inputs_0;values.combFinal1[0]=state.final_inputs_1;
   if(state.combiner_dynamic){state.combiner_control=0;state.final_inputs_0=state.final_inputs_1=0;}
   values.clipRange[0][1]=values.clipRange[0][3]=16777215;values.surfaceScale[0][0]=values.surfaceScale[0][1]=1;values.alphaKillMask[0]=(f/32)%16;
   for(unsigned j=0;j<8;j++){values.clipRegion[j][2]=4;values.clipRegion[j][3]=4;}
   for(unsigned j=0;j<4;j++){
    values.texScale[j]=.5f+(j%3)*.5f;values.colorKeyMask[j]=(f&128)?0xff000000u:0;values.colorKey[j]=0x80000000u;
    for(unsigned k=0;k<3;k++){values.borderLogicalSize[j][k]=4+j*.5f;values.borderInvRealSize[j][k]=1.0f/(12+j*2);}
    unsigned kind=fixture_kind(&state,j);VkDescriptorImageInfo info={samplers[(f/16+j)%16],kind==1?volume.view:kind==2?cube.view:texture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=2+j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&info};vkUpdateDescriptorSets(device,1,&w,0,NULL);
   }
   float plane[3][4]={{0,0,100,1},{4,0,200,1},{0,4,300,1}};memcpy(data[4],plane,sizeof(plane));data[7][1]=0;
  }
  GlslcResult modules[2];SpvPshOptions opts={.spirv_1_6=!!(test&256),.ubo_binding=1,.tex_binding=2};assert(pgraph_spv_psh(&state,opts,&modules[1]));MString *source=pgraph_glsl_gen_psh(&state,(GenPshGlslOptions){.vulkan=true,.locations=true,.ubo_binding=1,.tex_binding=2});GlslcOptions go={.spirv_1_6=opts.spirv_1_6,.emit_layout=true};if(!glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(source),&go,&modules[0])){fprintf(stderr,"fragment %u %d %s\n",test,modules[0].error_line,modules[0].error);abort();}reference_source_bytes+=strlen(mstring_get_str(source));mstring_unref(source);
  words[0]+=modules[0].num_words;words[1]+=modules[1].num_words;
  uint32_t expected[80];uint64_t build_ns[2]={0},draw_ns[2]={0};
  for(unsigned path=0;path<2;path++){
   assert(modules[path].uniforms.size<=2048);memset(uniform.mapped,0,2048);pack(uniform.mapped,&modules[path].uniforms,&values);VkShaderModule frag=module(&modules[path]);
   VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertices[test>=18944][state.smooth_shading],.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=frag,.pName="main"}};
   VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=state.point_sprite?VK_PRIMITIVE_TOPOLOGY_POINT_LIST:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};VkViewport viewport={0,0,4,4,0,1};VkRect2D scissor={{0,0},{4,4}};VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_ALWAYS};VkPipelineColorBlendAttachmentState blend={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&ds,.pColorBlendState=&cb,.layout=layout,.renderPass=pass};VkPipeline pipeline;uint64_t build_start=timings?wall_ns():0;CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gp,NULL,&pipeline));if(timings)build_ns[path]=wall_ns()-build_start;
   CHECK(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));VkClearValue clear[2]={{{{-2,3,5,7}}}, {.depthStencil={1,0}}};VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{4,4}},.clearValueCount=2,.pClearValues=clear};vkCmdBeginRenderPass(cmd,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,128,data);vkCmdDraw(cmd,state.point_sprite?1:3,1,0,0);vkCmdEndRenderPass(cmd);
   VkBufferImageCopy copies[2]={{.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={4,4,1}},{.bufferOffset=256,.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={4,4,1}}};vkCmdCopyImageToBuffer(cmd,color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,copies);vkCmdCopyImageToBuffer(cmd,depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,copies+1);CHECK(vkEndCommandBuffer(cmd));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};uint64_t draw_start=timings?wall_ns():0;CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));if(timings)draw_ns[path]=wall_ns()-draw_start;
   if(!path)memcpy(expected,readback.mapped,320);else for(unsigned j=0;j<80;j++){uint32_t got;float x,y;memcpy(&got,readback.mapped+j*4,4);memcpy(&x,&expected[j],4);memcpy(&y,&got,4);compared++;if(got!=expected[j] && !(isnan(x)&&isnan(y))){if(failures++<25)fprintf(stderr,"case %u output %u: %08x (%g) != %08x (%g)\n",test,j,expected[j],x,got,y);}}
   vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,frag,NULL);
  }
  if(timings)fprintf(stderr,"PSH timing case=%u ref_build_ns=%llu native_build_ns=%llu ref_submit_fence_ns=%llu native_submit_fence_ns=%llu UNQUALIFIED\n",test,(unsigned long long)build_ns[0],(unsigned long long)build_ns[1],(unsigned long long)draw_ns[0],(unsigned long long)draw_ns[1]);
  bool sampled=false;for(unsigned j=0;j<4;j++)sampled|=((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_PROJECT2D || ((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_PROJECT3D || ((state.shader_stage_program>>(j*5))&31)==PS_TEXTUREMODES_CUBEMAP;
  for(unsigned j=0;j<16;j++){if(expected[j*4]==0xc0000000u)discarded++;else{written++;sample_pixels+=sampled;point_pixels+=state.point_sprite;}}
  release(&modules[0]);release(&modules[1]);
 }
 printf("Fragment GPU: %u scalar color/depth/discard results, %u differences, %u validation errors; validation=%s\n",compared,failures,errors,validated?"enabled":"DISABLED");
 if(first>=16896)assert(written && sample_pixels);
 if(first>=4096 && (first<10240 || (first>=14336 && first<16384)))assert(written && discarded && sample_pixels);
 if(first>=8192 && (first<10240 || (first>=14336 && first<16384)))assert(point_pixels);
 printf("Fragment coverage: written_pixels=%u discarded_pixels=%u sampled_pixels=%u point_pixels=%u\n",written,discarded,sample_pixels,point_pixels);
 printf("Fragment work: reference_words=%llu native_words=%llu avoided_GLSL_bytes=%llu; work counts only, no latency/GPU execution claim\n",(unsigned long long)words[0],(unsigned long long)words[1],(unsigned long long)reference_source_bytes);
 for(unsigned j=0;j<2;j++){for(unsigned k=0;k<2;k++)vkDestroyShaderModule(device,vertices[j][k],NULL);}
 vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,commands,NULL);vkDestroyDescriptorPool(device,pool,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);vkDestroyFramebuffer(device,fb,NULL);vkDestroyRenderPass(device,pass,NULL);drop_image(color);drop_image(depth);drop(uniform);drop(readback);for(unsigned j=0;j<16;j++)vkDestroySampler(device,samplers[j],NULL);drop_image(texture);drop_image(volume);drop_image(cube);vkDestroyDevice(device,NULL);if(validated)destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);return failures||errors?1:0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include <spirv/unified1/spirv.h>
#include "hw/xbox/nv2a/pgraph/spv/vsh.h"
#define CHECK(x) do { VkResult result=(x); if(result) { fprintf(stderr,"Vulkan %d at %d\n",result,__LINE__);abort(); } } while(0)
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
/* Test-only TF decorations capture stage outputs before clipping/rasterization.
 * No semantic instruction, result ID or arithmetic is changed. */
static uint32_t *xfb(const GlslcResult *r,size_t *length) {
 uint32_t vars[15]={0},entry=0,block=0,members[2]={0};size_t annotation=0,execution=0,capability=5;
 for(size_t p=5;p<r->num_words;p+=r->words[p]>>16) {
  const uint32_t *a=r->words+p;unsigned op=a[0]&65535;
  if(op==SpvOpCapability)capability=p+(a[0]>>16);
  if(op==SpvOpEntryPoint){entry=a[2];execution=p+(a[0]>>16);}
  if(!annotation && op==SpvOpTypeVoid)annotation=p;
  if(op==SpvOpDecorate && a[2]==SpvDecorationBuiltIn) {
   if(a[3]==SpvBuiltInPosition)vars[13]=a[1];
   if(a[3]==SpvBuiltInPointSize)vars[14]=a[1];
  }
  if(op==SpvOpMemberDecorate && a[3]==SpvDecorationBuiltIn) {
   if(a[4]==SpvBuiltInPosition){block=a[1];members[0]=a[2];}
   if(a[4]==SpvBuiltInPointSize){assert(!block||block==a[1]);block=a[1];members[1]=a[2];}
  }
  if(op==SpvOpDecorate && a[2]==SpvDecorationLocation && a[3]<13) {
   for(size_t q=5;q<r->num_words;q+=r->words[q]>>16)if((r->words[q]&65535)==SpvOpVariable && r->words[q+2]==a[1] && r->words[q+3]==SpvStorageClassOutput)vars[a[3]]=a[1];
  }
 }
 uint32_t block_var=0;
 if(block)for(size_t p=5;p<r->num_words;p+=r->words[p]>>16) {
  const uint32_t *a=r->words+p;
  if((a[0]&65535)==SpvOpTypePointer && a[2]==SpvStorageClassOutput && a[3]==block)
   for(size_t q=5;q<r->num_words;q+=r->words[q]>>16)if((r->words[q]&65535)==SpvOpVariable && r->words[q+1]==a[1])block_var=r->words[q+2];
 }
 assert(entry && annotation && execution);for(unsigned i=0;i<(block?13u:15u);i++)assert(vars[i]);if(block)assert(block_var);
 *length=r->num_words+2+3+(block?13*12+8+10:15*12);uint32_t *out=malloc(*length*4);assert(out);size_t n=0;
 for(size_t p=0;p<r->num_words;p++) {
  if(p==capability){out[n++]=(2u<<16)|SpvOpCapability;out[n++]=SpvCapabilityTransformFeedback;}
  if(p==execution){out[n++]=(3u<<16)|SpvOpExecutionMode;out[n++]=entry;out[n++]=SpvExecutionModeXfb;}
  if(p==annotation){
   for(unsigned i=0;i<(block?13u:15u);i++) {
    const unsigned dec[]={SpvDecorationXfbBuffer,SpvDecorationXfbStride,SpvDecorationOffset};const unsigned val[]={0,240,i*16};
    for(unsigned j=0;j<3;j++){out[n++]=(4u<<16)|SpvOpDecorate;out[n++]=vars[i];out[n++]=dec[j];out[n++]=val[j];}
   }
   if(block){
    out[n++]=(4u<<16)|SpvOpDecorate;out[n++]=block_var;out[n++]=SpvDecorationXfbBuffer;out[n++]=0;
    out[n++]=(4u<<16)|SpvOpDecorate;out[n++]=block_var;out[n++]=SpvDecorationXfbStride;out[n++]=240;
    for(unsigned i=0;i<2;i++){out[n++]=(5u<<16)|SpvOpMemberDecorate;out[n++]=block;out[n++]=members[i];out[n++]=SpvDecorationOffset;out[n++]=(13+i)*16;}
   }
  }
  out[n++]=r->words[p];
 }
 assert(n==*length);return out;
}
static void token(uint32_t t[4],unsigned m,unsigned l,unsigned input,unsigned output,bool scalar,bool final) {
 t[0]=0;t[1]=(l<<25)|(m<<21)|(32u<<13)|(input<<9)|0x1b;
 t[2]=(PARAM_V<<26)|(PARAM_C<<11)|(0x1bu<<17)|(0x1bu<<2);
 t[3]=(PARAM_V<<28)|(15u<<24)|(2u<<20)|(15u<<16)|(15u<<12)|(OUTPUT_O<<11)|(output<<3)|((unsigned)scalar<<2)|final;
 if(m==MAC_ARL)t[3]&=~(15u<<12);
}
static void pack(uint8_t *dst,const GlslcLayout *layout,const VshUniformValues *values) {
 memset(dst,0,layout->size);
 for(size_t i=0;i<layout->count;i++) {
  const GlslcUniform *u=layout->members+i;unsigned j=0;
  for(;j<VshUniform__COUNT;j++)if(!strcmp(u->name,VshUniformInfo[j].name))break;
  assert(j<VshUniform__COUNT);const UniformInfo *info=VshUniformInfo+j;
  for(size_t k=0;k<u->dim_a;k++){assert(k<info->count);memcpy(dst+u->offset+k*u->stride,(const uint8_t *)values+info->val_offs+k*info->size,info->size);}
 }
}
static void release(GlslcResult *r){glslc_result_free_words(r);free(r->uniforms.members);free(r->push_constants.members);}
int main(void) {
 const bool validated=getenv("NO_VALIDATION")==NULL;
 const char *layer="VK_LAYER_KHRONOS_validation",*debug=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_2};
 VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledLayerCount=1,.ppEnabledLayerNames=&layer,.enabledExtensionCount=1,.ppEnabledExtensionNames=&debug};
 if(!validated){ici.enabledLayerCount=ici.enabledExtensionCount=0;}
 VkInstance instance;CHECK(vkCreateInstance(&ici,NULL,&instance));
 PFN_vkCreateDebugUtilsMessengerEXT create=(void *)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");
 PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void *)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");
 VkDebugUtilsMessengerCreateInfoEXT mi={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=7,.pfnUserCallback=validation};
 VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;if(validated){assert(create&&destroy);CHECK(create(instance,&mi,NULL,&messenger));}
 uint32_t n=1;VkPhysicalDevice gpu;CHECK(vkEnumeratePhysicalDevices(instance,&n,&gpu));vkGetPhysicalDeviceMemoryProperties(gpu,&memory_properties);
 VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(gpu,&props);puts(props.deviceName);
 VkPhysicalDeviceTransformFeedbackFeaturesEXT tf={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT};
 VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&tf};vkGetPhysicalDeviceFeatures2(gpu,&features);
 if(!tf.transformFeedback){fputs("UNQUALIFIED: transform feedback unavailable\n",stderr);return 2;}
 tf.geometryStreams=VK_FALSE;features.features=(VkPhysicalDeviceFeatures){0};
 uint32_t nq=32;VkQueueFamilyProperties families[32];vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,families);unsigned family=0;
 while(family<nq && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)){family++;}
 assert(family<nq);float priority=1;
 VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
 const char *extension=VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME;
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&features,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=1,.ppEnabledExtensionNames=&extension};
 CHECK(vkCreateDevice(gpu,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
 PFN_vkCmdBindTransformFeedbackBuffersEXT bind=(void *)vkGetDeviceProcAddr(device,"vkCmdBindTransformFeedbackBuffersEXT");
 PFN_vkCmdBeginTransformFeedbackEXT begin=(void *)vkGetDeviceProcAddr(device,"vkCmdBeginTransformFeedbackEXT");
 PFN_vkCmdEndTransformFeedbackEXT end=(void *)vkGetDeviceProcAddr(device,"vkCmdEndTransformFeedbackEXT");assert(bind&&begin&&end);
 VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VkCommandPool pool;CHECK(vkCreateCommandPool(device,&pi,NULL,&pool));
 VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cmd;CHECK(vkAllocateCommandBuffers(device,&ca,&cmd));
 VkDescriptorSetLayoutBinding db={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT};
 VkDescriptorSetLayoutCreateInfo dli={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&db};VkDescriptorSetLayout set_layout;CHECK(vkCreateDescriptorSetLayout(device,&dli,NULL,&set_layout));
 VkPushConstantRange pc={VK_SHADER_STAGE_VERTEX_BIT,0,props.limits.maxPushConstantsSize>=256?256:128};
 VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&pc};VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&pli,NULL,&layout));
 VkDescriptorPoolSize dp={VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&dp};VkDescriptorPool descriptor_pool;CHECK(vkCreateDescriptorPool(device,&dpi,NULL,&descriptor_pool));
 VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&dai,&set));
 VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.subpassCount=1,.pSubpasses=&sub};VkRenderPass pass;CHECK(vkCreateRenderPass(device,&ri,NULL,&pass));
 VkFramebufferCreateInfo fbi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.width=1,.height=1,.layers=1};VkFramebuffer framebuffer;CHECK(vkCreateFramebuffer(device,&fbi,NULL,&framebuffer));
 enum{VERTICES=32};Buffer input=buffer(VERTICES*256,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),output=buffer(VERTICES*240,VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT),uniform=buffer(16384,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
 VkDescriptorBufferInfo info={uniform.handle,0,VK_WHOLE_SIZE};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&info};vkUpdateDescriptorSets(device,1,&write,0,NULL);
 VkVertexInputBindingDescription binding={0,256,VK_VERTEX_INPUT_RATE_VERTEX};VkVertexInputAttributeDescription attrs[16];
 for(unsigned i=0;i<16;i++)attrs[i]=(VkVertexInputAttributeDescription){i,0,VK_FORMAT_R32G32B32A32_SFLOAT,i*16};
 VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&binding,.vertexAttributeDescriptionCount=16,.pVertexAttributeDescriptions=attrs};
 VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
 VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.rasterizerDiscardEnable=VK_TRUE,.lineWidth=1};
 VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
 uint32_t reference[VERTICES*60];unsigned compared=0,failures=0;
 clock_t reference_ticks=0,direct_ticks=0;size_t reference_words=0,direct_words=0;
 for(unsigned test=0;test<896;test++) {
  VshState state={0};state.smooth_shading=test&1;state.fog_dynamic=true;state.specular_enable=test&2;state.ignore_specular_alpha=test&4;
  unsigned variant=test/112,m=test%14,l=(test%112)/14;state.programmable.program_length=5;
  token(state.programmable.program_data[0],MAC_MOV,ILU_NOP,0,0,false,false);
  token(state.programmable.program_data[1],m,l,m==MAC_ARL?15:1,9,!!(test&1),false);
  token(state.programmable.program_data[2],MAC_MOV,ILU_NOP,0,10,false,false);
  state.programmable.program_data[2][2]=(state.programmable.program_data[2][2]&~(3u<<26))|(PARAM_R<<26)|(2u<<28);
  token(state.programmable.program_data[3],MAC_MOV,ILU_NOP,0,11,false,false);
  state.programmable.program_data[3][2]=(state.programmable.program_data[3][2]&~(3u<<26))|(PARAM_R<<26)|(1u<<28);
  token(state.programmable.program_data[4],MAC_MOV,ILU_NOP,1,5,false,true);
  SpvVshOptions opts={.ubo_binding=0};
  if(variant==1) {
   uint32_t *t=state.programmable.program_data[1];
   t[3]=(t[3]&~0x0f0ff000u)|((test%16)<<24)|(((test*7)%16)<<16)|(((test*13)%16)<<12);
   if(m==MAC_ARL)t[3]&=~(15u<<12);
  }
  if(variant==2)state.uniform_attrs=3;
  if(variant==3){state.uniform_attrs=props.limits.maxPushConstantsSize>=256?0xffff:0xff;opts.push_constants=true;}
  if(variant==4){state.compressed_attrs=2;state.swizzle_attrs=4;state.programmable.program_data[4][1]=(state.programmable.program_data[4][1]&~(15u<<9))|(2u<<9);}
  if(variant==5 && m!=MAC_ARL){
   uint32_t *t=state.programmable.program_data[1];
   t[2]=(t[2]&~(0xfcu<<24))|(12u<<28)|(PARAM_R<<26);
   t[2]=(t[2]&~3u)|3u;t[3]&=~(3u<<30);
   t[3]=(t[3]&~((15u<<3)|(15u<<20)|(3u<<28)))|(12u<<20)|(PARAM_R<<28);
  }
  if(variant==6){
   state.programmable.program_data[4][3]&=~1u;state.programmable.program_length=6;
   token(state.programmable.program_data[5],MAC_MOV,ILU_NOP,0,12,false,true);
   state.programmable.program_data[5][2]=(state.programmable.program_data[5][2]&~(3u<<26))|(PARAM_C<<26);
   state.programmable.program_data[5][3]|=2;
  }
  if(variant==7){
   state.fog_dynamic=false;
   uint32_t *t=state.programmable.program_data[1];
   unsigned output=3+(test%6);if(m!=MAC_ARL)t[3]=(t[3]&~(15u<<3))|(output<<3);
   t[1]^=(test%256);t[2]^=((test%256)<<17)|((test%256)<<2);
   if(m!=MAC_ARL)t[1]|=1u<<8;
   t[2]|=(1u<<25)|(1u<<10);
  }
  for(unsigned a=0;a<16;a++)attrs[a].format=state.compressed_attrs&(1u<<a)?VK_FORMAT_R32_SINT:VK_FORMAT_R32G32B32A32_SFLOAT;
  VshState excluded=state;excluded.point_params_enable=true;
  assert(!pgraph_spv_vsh_supported(&excluded,opts));
  GlslcResult modules[2];clock_t before=clock();assert(pgraph_spv_vsh(&state,opts,&modules[1]));direct_ticks+=clock()-before;direct_words+=modules[1].num_words;before=clock();
  MString *text=pgraph_glsl_gen_vsh(&state,(GenVshGlslOptions){.vulkan=true,.use_push_constants_for_uniform_attrs=opts.push_constants});
  GlslcOptions glopts={.emit_layout=true};if(!glslc_compile(GLSLC_STAGE_VERTEX,mstring_get_str(text),&glopts,&modules[0])){fprintf(stderr,"%s\n",modules[0].error);abort();}mstring_unref(text);reference_ticks+=clock()-before;reference_words+=modules[0].num_words;
  VshUniformValues values={0};values.clipRange[0][1]=65535;values.surfaceSize[0][0]=720;values.surfaceSize[0][1]=480;values.pointSizeReg[0]=1;values.pointScale[0]=1;values.fogCtl[0]=1|((test%8)<<1);values.fogParam[0][0]=1.5f;values.fogParam[0][1]=-.01f;
  for(unsigned i=0;i<192;i++)for(unsigned k=0;k<4;k++)values.c[i][k]=((int)((i*7+k*13)%37)-18)*.125f;
  unsigned packed=0;for(unsigned a=0;a<16;a++)if(state.uniform_attrs&(1u<<a)) {
   for(unsigned k=0;k<4;k++)values.inlineValue[packed][k]=a==0?(k==3?1:k==2?100:40):(a==15?2:((int)(a+k)%7-3)*.5f);
   packed++;
  }
  const float samples[]={0,-0.0f,1,-1,2,-2,.5f,-.5f,4,8,16,-32,INFINITY,-INFINITY,NAN,0x1p-100f};
  for(unsigned v=0;v<VERTICES;v++)for(unsigned a=0;a<16;a++)for(unsigned k=0;k<4;k++) {
   float f=samples[(v+a*3+k*5)%16];if(a==0)f=k==3?1:k==2?100:(float)(v*3+40);if(a==15)f=(float)(v%8);
   memcpy(input.mapped+v*256+a*16+k*4,&f,4);
  }
  for(unsigned path=0;path<2;path++) {
   pack(uniform.mapped,&modules[path].uniforms,&values);memset(output.mapped,0xa5,VERTICES*240);
   size_t words;uint32_t *code=xfb(&modules[path],&words);
   VkShaderModuleCreateInfo smi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=words*4,.pCode=code};VkShaderModule shader;CHECK(vkCreateShaderModule(device,&smi,NULL,&shader));free(code);
   VkPipelineShaderStageCreateInfo stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=shader,.pName="main"};
   VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=1,.pStages=&stage,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pRasterizationState=&rs,.pMultisampleState=&ms,.layout=layout,.renderPass=pass};VkPipeline pipeline;CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gp,NULL,&pipeline));
   VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&bi));VkRenderPassBeginInfo rb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea.extent={1,1}};
   vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);
   VkDeviceSize zero=0;vkCmdBindVertexBuffers(cmd,0,1,&input.handle,&zero);
   if(modules[path].push_constants.count){uint8_t push[256];pack(push,&modules[path].push_constants,&values);vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,(uint32_t)modules[path].push_constants.size,push);}
   bind(cmd,0,1,&output.handle,&zero,NULL);begin(cmd,0,0,NULL,NULL);vkCmdDraw(cmd,VERTICES,1,0,0);end(cmd,0,0,NULL,NULL);vkCmdEndRenderPass(cmd);
   VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=output.handle,.size=VK_WHOLE_SIZE};
   vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);CHECK(vkEndCommandBuffer(cmd));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fi,NULL,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
   if(!path)memcpy(reference,output.mapped,sizeof(reference));else for(unsigned v=0;v<VERTICES;v++)for(unsigned a=0;a<15;a++)for(unsigned k=0;k<(a==4||a==12||a==14?1u:4u);k++) {
    unsigned index=v*60+a*4+k;uint32_t got;memcpy(&got,output.mapped+index*4,4);float x,y;memcpy(&x,&reference[index],4);memcpy(&y,&got,4);compared++;
    if(got!=reference[index] && !(isnan(x)&&isnan(y))){if(failures++<25)fprintf(stderr,"case %u MAC %u ILU %u vertex %u output %u.%u: %08x (%g) != %08x (%g)\n",test,m,l,v,a,k,reference[index],x,got,y);}
   }
   vkDestroyFence(device,fence,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,shader,NULL);CHECK(vkResetCommandBuffer(cmd,0));
  }
  release(&modules[0]);release(&modules[1]);
 }
 drop(input);drop(output);drop(uniform);vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyDescriptorPool(device,descriptor_pool,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);if(validated)destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
 printf("Translation CPU clock: GLSL %.3f ms, direct %.3f ms; emitted words GLSL=%zu direct=%zu (work counts; not frame latency)\n",1000.0*reference_ticks/CLOCKS_PER_SEC,1000.0*direct_ticks/CLOCKS_PER_SEC,reference_words,direct_words);
 printf("Direct VSH Vulkan: %u scalar results, %u differences, %u validation errors; validation=%s\n",compared,failures,errors,validated?"enabled":"DISABLED");return failures||errors?1:0;
}

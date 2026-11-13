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
/* Numerical fixture only: retain identical driver stage compilation per batch. */
static VkPipelineCache pipeline_cache;
static VkPhysicalDeviceMemoryProperties memory_properties;
static unsigned errors,constant_checks;
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
/* Test reference only: request the same defined FP32 environment as the
 * direct point shader. Arithmetic, stores and metadata remain unchanged. */
static uint32_t *preserve_float(const GlslcResult *r,size_t *length) {
 *length=r->num_words+6;uint32_t *out=malloc(*length*4);assert(out);size_t n=0;bool cap=false;
 for(size_t p=0;p<5;p++)out[n++]=r->words[p];
 for(size_t p=5;p<r->num_words;){const uint32_t *a=r->words+p;unsigned op=a[0]&65535,wc=a[0]>>16;
  if(!cap && op!=SpvOpCapability){out[n++]=(2u<<16)|SpvOpCapability;out[n++]=SpvCapabilitySignedZeroInfNanPreserve;cap=true;}
  for(unsigned i=0;i<wc;i++)out[n++]=a[i];
  if(op==SpvOpEntryPoint){out[n++]=(4u<<16)|SpvOpExecutionMode;out[n++]=a[2];out[n++]=SpvExecutionModeSignedZeroInfNanPreserve;out[n++]=32;}
  p+=wc;
 }
 assert(n==*length);return out;
}
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
static void unused_c(uint32_t t[4], unsigned kind)
{
    if(kind<3){unsigned reg=13+kind;t[3]=(t[3]&~(15u<<28))|((reg&3)<<30)|(PARAM_R<<28);t[2]=(t[2]&~3u)|(reg>>2);}
    else{t[1]=(t[1]&~(255u<<13))|((kind==3?192u:255u)<<13);t[3]=(t[3]&~(3u<<28))|(PARAM_C<<28);}
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
/* Test-only guard against mutating the recipe/options after module generation.
 * Hash only borrowed bytes; this is not a persistent or runtime cache key. */
static uint64_t fixture_key_hash(const VshState *s,const SpvVshOptions *o){
 const uint8_t *p=(const uint8_t *)s;uint64_t h=1469598103934665603ull;
 for(size_t i=0;i<sizeof(*s);i++)h=(h^p[i])*1099511628211ull;
 h=(h^o->spirv_1_6)*1099511628211ull;h=(h^o->preserve_float32)*1099511628211ull;
 h=(h^o->push_constants)*1099511628211ull;return (h^o->ubo_binding)*1099511628211ull;
}
/* Construct an instruction with one decoded but unconsumed operand. */
static unsigned masked_operand(uint32_t t[4], unsigned variant, bool invalid)
{
    unsigned style=variant%6, kind=(variant/6)%5, arg=style==1||style>=4?2:0;
    unsigned m=style==1?0:style==4?MAC_MOV:style==5?MAC_ARL:1+(variant/30)%12;
    unsigned l=style==0?0:1+(variant/360)%7;
    token(t,m,l,m==MAC_ARL?15:1,9,style==2||style==3,false);
    t[2]=(t[2]&~(3u<<11))|(PARAM_V<<11);
    t[3]&=~((15u<<24)|(15u<<16)|(15u<<12));
    if(style==2||style==3)t[3]|=(15u<<16)|(15u<<12);
    if(style==3)t[3]=(t[3]&~(15u<<20))|(1u<<20)|(15u<<24);
    if(style==4)t[3]|=(15u<<24)|(15u<<12);
    if(kind>=3)t[1]=(t[1]&~(255u<<13))|((kind==3?192u:255u)<<13);
    unsigned source=invalid?(kind<3?PARAM_R:PARAM_C):PARAM_V, reg=13+kind%3;
    if(arg==0)t[2]=(t[2]&~((15u<<28)|(3u<<26)))|(reg<<28)|(source<<26);
    else {t[2]=(t[2]&~3u)|(reg>>2);t[3]=(t[3]&~(15u<<28))|((reg&3)<<30)|(source<<28);}
    return arg;
}

/* The output mux selects the NOP pipe; temporary/ARL writes remain live. */
static void unselected_output(uint32_t t[4], unsigned variant, bool invalid)
{
    unsigned pipe=(variant/21)%2, dest=variant%21;
    unsigned m=pipe?0:1+(variant/42)%13, l=pipe?1+(variant/42)%7:0;
    token(t,m,l,m==MAC_ARL?15:1,0,!pipe,false);
    t[3]|=15u<<12;
    if(invalid){
        unsigned output=dest<5?(dest<2?dest+1:dest+11):dest-5;
        t[3]=(t[3]&~(15u<<3))|(output<<3);
        if(dest>=5)t[3]&=~(1u<<11);
    }
}

int main(int argc, char **argv) {
 assert(argc==1 || argc==3 || argc==5);
 unsigned first=argc>=3?strtoul(argv[1],NULL,10):0, limit=argc>=3?strtoul(argv[2],NULL,10):42512;
 assert(first<limit && limit<=42512);
 printf("Fixture range [%u, %u)\n",first,limit);
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
 VkPhysicalDeviceFloatControlsProperties fp={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES};
 VkPhysicalDeviceProperties2 pp={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&fp};vkGetPhysicalDeviceProperties2(gpu,&pp);
 if(!fp.shaderSignedZeroInfNanPreserveFloat32){fputs("UNQUALIFIED: FP32 exceptional-value preservation unavailable\n",stderr);return 2;}

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
 for(unsigned test=first;test<limit;test++) {
  /* Spread adjacent state families in 16-case chunks, rotating owners each
   * round so low-bit shader selectors do not remain pinned to one owner.
   * Every case belongs to exactly one slot; the parent owns one deadline. */
  if(argc==5){
   unsigned slot=strtoul(argv[3],NULL,10),slots=strtoul(argv[4],NULL,10);
   assert(slots>=1 && slots<=4 && slot<slots);
   unsigned block=(test-first)/16;
   if((block+block/slots)%slots!=slot)continue;
  }

  VshState state={0};state.smooth_shading=test&1;state.fog_dynamic=true;state.specular_enable=test&2;state.ignore_specular_alpha=test&4;
  unsigned variant=(test%1568)/112,m=test%14,l=(test%112)/14;state.programmable.program_length=5;
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
  if(variant>=8){
   static const unsigned modes[]={FOG_MODE_LINEAR,FOG_MODE_EXP,FOG_MODE_EXP2,FOG_MODE_LINEAR_ABS,FOG_MODE_EXP_ABS,FOG_MODE_EXP2_ABS};
   state.fog_dynamic=false;state.fog_enable=true;state.fog_mode=modes[variant-8];
   unsigned input_style=(test/14)%4;
   if(input_style==1 || input_style==2){state.uniform_attrs=3;opts.push_constants=input_style==2;}
   if(input_style==3){state.compressed_attrs=2;state.swizzle_attrs=4;}
  }
  state.point_params_enable=test>=1568;
  if(state.point_params_enable && variant==1 && m!=MAC_ARL){
   uint32_t *t=state.programmable.program_data[1];t[3]=(t[3]&~(15u<<3))|(6u<<3);
  }
  if(test>=3136){
   unsigned f=test-3136;
   state=(VshState){.is_fixed_function=true,.smooth_shading=!!(f&1),.specular_enable=!!(f&2),.ignore_specular_alpha=!!(f&4),.separate_specular=!!(f&8)};
   state.fixed_function.normalization=!!(f&16);state.fixed_function.light_dynamic=!!(f&32);state.fixed_function.two_sided=!!(f&64);
   for(unsigned t=0;t<4;t++)state.fixed_function.texture_matrix_enable[t]=!!(f&(1u<<t));
   state.programmable.program_length=0x7fffffff; /* Must not consume microcode. */
   opts.push_constants=false;
   unsigned style=(f/16)%5;
   if(style==1)state.uniform_attrs=0x1f99;
   if(style==2){state.uniform_attrs=props.limits.maxPushConstantsSize>=256?0xffff:0xff;opts.push_constants=true;}
   if(style==3){state.compressed_attrs=(1<<0)|(1<<9)|(1<<11);state.swizzle_attrs=(1<<3)|(1<<10)|(1<<12);}
   if(style==4)state.uniform_attrs=0xffff;
   if(test>=4160){
    static const unsigned modes[]={FOG_MODE_LINEAR,FOG_MODE_EXP,FOG_MODE_EXP2,FOG_MODE_LINEAR_ABS,FOG_MODE_EXP_ABS,FOG_MODE_EXP2_ABS};
    unsigned fog=(test-4160)%24;state.fog_enable=true;
    if(fog<12){state.fog_mode=modes[fog%6];state.fixed_function.foggen=fog<6?FOGGEN_SPEC_ALPHA:FOGGEN_FOG_X;}
    else {static const unsigned linear_exp[]={FOG_MODE_LINEAR,FOG_MODE_EXP,FOG_MODE_LINEAR_ABS,FOG_MODE_EXP_ABS};state.fog_mode=linear_exp[(fog-12)%4];state.fixed_function.foggen=FOGGEN_RADIAL+(fog-12)/4;}
   }
  }
  if(test>=5120){
   unsigned f=test-5120;state.fog_enable=!!((f/16)&1);
   for(unsigned t=0;t<4;t++){
    state.fixed_function.texture_matrix_enable[t]=!!(f&(1u<<t));
    unsigned pattern=((f/16)+t*17)%81;
    for(unsigned a=0;a<4;a++){state.fixed_function.texgen[t][a]=pattern%3;pattern/=3;}
   }
  }
  if(test>=6416){
   unsigned f=test-6416;
   for(unsigned t=0;t<4;t++){
    state.fixed_function.texture_matrix_enable[t]=!!(f&(1u<<t));
    unsigned pattern=((f/16)+t*37)%192;
    for(unsigned a=0;a<3;a++){unsigned mode=pattern%4;pattern/=4;state.fixed_function.texgen[t][a]=mode==3?TEXGEN_NORMAL_MAP:mode;}
    state.fixed_function.texgen[t][3]=pattern;
   }
   if(state.compressed_attrs)state.compressed_attrs|=1u<<2;
  }
  if(test>=9488){
   unsigned f=test-9488, pattern=f/16;
   for(unsigned t=0;t<4;t++){
    state.fixed_function.texture_matrix_enable[t]=!!(f&(1u<<t));
    unsigned n=(pattern+t*43)%128;
    state.fixed_function.texgen[t][0]=n%6;
    state.fixed_function.texgen[t][1]=(n/6+t)%6;
    static const enum VshTexgen rmode[]={TEXGEN_DISABLE,TEXGEN_EYE_LINEAR,TEXGEN_OBJECT_LINEAR,TEXGEN_NORMAL_MAP,TEXGEN_REFLECTION_MAP};
    state.fixed_function.texgen[t][2]=rmode[(n/11+t)%5];
    state.fixed_function.texgen[t][3]=(n/7+t)%3;
   }
  }
  if(test>=11536)state.point_params_enable=true;
  if(test>=12560){
   unsigned f=test-12560;
   state.fixed_function.skinning=1+f/256;
   state.point_params_enable=!!(f&32);
   if(state.compressed_attrs)state.compressed_attrs|=1u<<1;
  }
  if(test>=14096){
   unsigned f=test-14096;state.fixed_function.texgen_dynamic=true;
   state.fixed_function.skinning=(f/16)%7;state.point_params_enable=!!(f&512);
   state.fixed_function.normalization=!!(f&256);
   memset(state.fixed_function.texgen,0xa5,sizeof(state.fixed_function.texgen));
  }
  if(test>=15120){
   unsigned f=test-15120;state.fog_dynamic=true;
   state.fog_enable=!(f&1024);state.fog_mode=(enum VshFogMode)-1;
   state.fixed_function.foggen=(enum VshFoggen)-1;
   state.fixed_function.texgen_dynamic=!!(f&1);
   state.fixed_function.skinning=(f/16)%7;
   state.point_params_enable=!!(f&8);state.fixed_function.normalization=!!(f&2);
   for(unsigned t=0;t<4;t++){
    state.fixed_function.texture_matrix_enable[t]=!!(f&(1u<<t));
    state.fixed_function.texgen[t][0]=(f/16+t)%6;
    state.fixed_function.texgen[t][1]=(f/32+t)%6;
    state.fixed_function.texgen[t][2]=TEXGEN_NORMAL_MAP;
    state.fixed_function.texgen[t][3]=TEXGEN_OBJECT_LINEAR;
   }
  }
  if(test>=19216){
   unsigned f=test-19216, combo=f/128;
   state.fog_dynamic=false;state.fog_enable=true;
   state.fog_mode=combo<3?FOG_MODE_EXP2:FOG_MODE_EXP2_ABS;
   state.fixed_function.foggen=FOGGEN_RADIAL+combo%3;
   state.fixed_function.texgen_dynamic=!!(f&64);state.point_params_enable=!!(f&32);
  }
  if(test>=19984){
   unsigned f=test-19984, material=(f/7)%81;
   state.fixed_function.lighting=true;state.fixed_function.light_dynamic=false;
   state.fixed_function.two_sided=!!(f&256);state.fixed_function.local_eye=!!(f&512);
   state.specular_enable=!!(f&1024);state.separate_specular=!!(f&2048);
   state.fixed_function.skinning=(f/16)%7;
   state.fixed_function.ambient_src=material%3;material/=3;
   state.fixed_function.emission_src=material%3;material/=3;
   state.fixed_function.diffuse_src=material%3;material/=3;
   state.fixed_function.specular_src=material;
   for(unsigned l=0;l<8;l++)state.fixed_function.light[l]=f&(1u<<l)?LIGHT_INFINITE:LIGHT_OFF;
  }
  if(test>=24080){
   unsigned f=test-24080, pattern=f&255;
   state.fixed_function.two_sided=!!(f&256);state.fixed_function.local_eye=!!(f&512);
   state.specular_enable=!!(f&1024);state.separate_specular=!!(f&2048);
   state.fixed_function.skinning=(f/16)%7;
   for(unsigned l=0;l<8;l++){
    unsigned mode=(pattern>>((l%4)*2))&3;
    if(l>=4)mode=(mode+1)&3;
    if(pattern<16)mode=l==pattern%8?(pattern<8?LIGHT_LOCAL:LIGHT_SPOT):LIGHT_OFF;
    state.fixed_function.light[l]=mode;
   }
  }
  if(test>=28176){
   unsigned f=test-28176;state.fixed_function.light_dynamic=true;
   state.fixed_function.two_sided=!!(f&256);state.fixed_function.local_eye=!!(f&512);
   state.specular_enable=!!(f&1024);state.separate_specular=!!(f&2048);
   for(unsigned l=0;l<8;l++)state.fixed_function.light[l]=LIGHT_OFF;
  }
  if(test>=32272){
   state.fixed_function.csv0c_dynamic=true;
   state.fixed_function.lighting=!!(test&1);state.fixed_function.light_dynamic=!!(test&2);
  }
  if(test>=36368){
   unsigned f=test-36368;const unsigned modes[]={1,2,5,6,7,8,9,10,11,12,13};m=modes[f%11];l=0;memset(&state,0,sizeof(state));state.programmable.program_length=5;
   token(state.programmable.program_data[0],MAC_MOV,0,0,0,false,false);
   token(state.programmable.program_data[1],m,0,m==MAC_ARL?15:1,9,false,false);
   uint32_t *t=state.programmable.program_data[1];t[2]=(t[2]&~(3u<<11))|(PARAM_V<<11);unused_c(t,(f/11)%5);
   token(state.programmable.program_data[2],MAC_MOV,0,0,10,false,false);
   if(m==MAC_ARL){uint32_t *next=state.programmable.program_data[2];next[1]=(next[1]&~(255u<<13))|(96u<<13);next[2]=(next[2]&~(3u<<26))|(PARAM_C<<26);next[3]|=2;}
   token(state.programmable.program_data[3],MAC_MOV,0,0,11,false,false);token(state.programmable.program_data[4],MAC_MOV,0,1,5,false,true);
   state.smooth_shading=!!(f&16);state.uniform_attrs=f&32?3:0;opts.push_constants=!!(f&64);
  }
  if(test>=37392){
   unsigned f=test-37392;memset(&state,0,sizeof(state));state.programmable.program_length=5;
   token(state.programmable.program_data[0],MAC_MOV,0,0,0,false,false);
   masked_operand(state.programmable.program_data[1],f,true);
   token(state.programmable.program_data[2],MAC_MOV,0,0,10,false,false);
   if(f%6==5){uint32_t *next=state.programmable.program_data[2];next[1]=(next[1]&~(255u<<13))|(96u<<13);next[2]=(next[2]&~(3u<<26))|(PARAM_C<<26);next[3]|=2;}
   token(state.programmable.program_data[3],MAC_MOV,0,0,11,false,false);token(state.programmable.program_data[4],MAC_MOV,0,1,5,false,true);
   state.smooth_shading=!!(f&16);state.uniform_attrs=f&32?3:0;opts.push_constants=!!(f&64);
  }
  if(test>=38416){
   unsigned f=test-38416;memset(&state,0,sizeof(state));state.programmable.program_length=5;
   token(state.programmable.program_data[0],MAC_MOV,0,0,0,false,false);
   unselected_output(state.programmable.program_data[1],f,true);
   token(state.programmable.program_data[2],MAC_MOV,0,0,10,false,false);
   if(!((f/21)%2) && 1+(f/42)%13==MAC_ARL){uint32_t *next=state.programmable.program_data[2];next[1]=(next[1]&~(255u<<13))|(96u<<13);next[2]=(next[2]&~(3u<<26))|(PARAM_C<<26);next[3]|=2;}
   token(state.programmable.program_data[3],MAC_MOV,0,0,11,false,false);token(state.programmable.program_data[4],MAC_MOV,0,1,5,false,true);
   state.smooth_shading=!!(f&16);state.uniform_attrs=f&32?3:0;opts.push_constants=!!(f&64);
  }
  if(test>=39440){
   unsigned f=test-39440,arg=(f/64)%3,base=192+f%64;
   memset(&state,0,sizeof(state));state.programmable.program_length=5;
   token(state.programmable.program_data[0],MAC_MOV,0,0,0,false,false);
   token(state.programmable.program_data[1],MAC_ARL,0,15,0,false,false);
   uint32_t *t=state.programmable.program_data[2];token(t,arg==2?0:arg==1?MAC_MUL:MAC_MOV,arg==2?ILU_MOV:0,1,9,arg==2,false);
   t[1]=(t[1]&~(255u<<13))|(base<<13);t[2]=(t[2]&~(3u<<11))|(PARAM_V<<11);
   if(arg==0)t[2]=(t[2]&~(3u<<26))|(PARAM_C<<26);else if(arg==1)t[2]=(t[2]&~(3u<<11))|(PARAM_C<<11);else t[3]=(t[3]&~(3u<<28))|(PARAM_C<<28);t[3]|=2;
   token(state.programmable.program_data[3],MAC_MOV,0,0,10,false,false);token(state.programmable.program_data[4],MAC_MOV,0,1,5,false,true);
   state.smooth_shading=!!(f&16);state.uniform_attrs=f&32?0x8003:0;opts.push_constants=!!(f&64);
  }
  if(test>=40464){
   unsigned f=test-40464,base=32+f%128;
   memset(&state,0,sizeof(state));state.programmable.program_length=8;
   token(state.programmable.program_data[0],MAC_MOV,0,0,0,false,false);
   token(state.programmable.program_data[1],MAC_ARL,0,15,0,false,false);
   token(state.programmable.program_data[3],MAC_ARL,0,15,0,false,false);
   state.programmable.program_data[3][1]|=1u<<8; /* new A0: negate V15 */
   for(unsigned j=0;j<3;j++){
    unsigned inst=j==0?2:j==1?4:5;
    uint32_t *t=state.programmable.program_data[inst];
    token(t,MAC_MAD,(f/256)%8,0,9+j,false,false);
    t[1]=(t[1]&~((255u<<13)|255u))|(base<<13)|(f&255u);
    t[2]=PARAM_C<<26|PARAM_C<<11|((f*13u&255u)<<17)|((f*37u&255u)<<2);
    if(f&512)t[1]|=1u<<8;
    if(f&1024)t[2]|=1u<<25;
    if(f&256)t[2]|=1u<<10;
    t[3]=(t[3]&~(3u<<28))|PARAM_C<<28;
    if(j<2)t[3]|=2; /* same selector, distinct address SSA definitions */
   }
   token(state.programmable.program_data[6],MAC_MOV,0,0,5,false,false);
   token(state.programmable.program_data[7],MAC_MOV,0,1,3,false,true);
   state.smooth_shading=!!(f&16);state.uniform_attrs=f&32?0x8000:0;
   opts.push_constants=!!(f&64);
  }
  for(unsigned a=0;a<16;a++)attrs[a].format=state.compressed_attrs&(1u<<a)?VK_FORMAT_R32_SINT:VK_FORMAT_R32G32B32A32_SFLOAT;
  opts.preserve_float32=fp.shaderSignedZeroInfNanPreserveFloat32;
  VshState excluded=state;excluded.point_params_enable=true;
  SpvVshOptions unavailable=opts;unavailable.preserve_float32=false;
  assert(!pgraph_spv_vsh_supported(&excluded,unavailable));
  uint64_t prepared_hash=fixture_key_hash(&state,&opts);
  GlslcResult modules[2];clock_t before=clock();assert(pgraph_spv_vsh(&state,opts,&modules[1]));direct_ticks+=clock()-before;direct_words+=modules[1].num_words;before=clock();
  MString *text=pgraph_glsl_gen_vsh(&state,(GenVshGlslOptions){.vulkan=true,.use_push_constants_for_uniform_attrs=opts.push_constants});
  GlslcOptions glopts={.emit_layout=true};if(!glslc_compile(GLSLC_STAGE_VERTEX,mstring_get_str(text),&glopts,&modules[0])){fprintf(stderr,"%s\n",modules[0].error);abort();}mstring_unref(text);reference_ticks+=clock()-before;reference_words+=modules[0].num_words;
  VshUniformValues values={0};values.clipRange[0][1]=65535;values.surfaceSize[0][0]=720;values.surfaceSize[0][1]=480;values.pointSizeReg[0]=1;values.pointScale[0]=1;values.texgenMode[0]=(test*37u & 4095u) | (test&8?4096u:0u);values.fogCtl[0]=1|((test%8)<<1);values.fogParam[0][0]=1.5f;values.fogParam[0][1]=-.01f;
  if(test>=28176){
   unsigned f=test-28176;values.lightMode[0]=((f*40503u)&65535u) | ((f%9)<<16) | 0xa5f00000u;
   values.csv0cCtl[0]=(f&128?32768u:0u) | 0x5a000000u;
  }
  if(test>=32272){
   unsigned f=test-32272,row=f/256,flags=0;
   const unsigned forms[]={1,2,4,8,3,5,9},bits[]={0,12,13,14,15,1,2};
   for(unsigned k=0;k<7;k++)flags|=((__builtin_parity(row&forms[k]) ^ ((f>>k)&1u))<<bits[k]);
   values.csv0cCtl[0]=((f&255)<<4) | flags | 0xa5000008u;
   values.lightMode[0]=((f*40503u)&65535u) | ((f%9)<<16) | 0xa5f00000u;
  }
  if(state.fixed_function.texgen_dynamic){
   unsigned f=test-14096;
   values.texMatEnable[0]=(f&15u) | 0xffff0000u;
   for(unsigned t=0;t<4;t++){
    uint32_t packed=(f&128?4096u:0u) | 0xa5a50000u;
    for(unsigned a=0;a<4;a++)packed|=(((f/16)+t*17+a*3)%8)<<(a*3);
    values.texgenMode[t]=packed;
   }
  }
  if(state.point_params_enable){
   const float scales[]={0.0f,-0.0f,1.0f,2.0f,-1.0f,.5f,INFINITY,NAN};
   values.pointScale[0]=scales[(test/14)%8];
  }
  if(variant>=8 && !state.is_fixed_function){
   /* Static fog must ignore the live fogCtl, including its disabled bit. */
   values.fogCtl[0]=(test*13u)&15u;
   const float params[]={-.01f,0.0f,-0.0f,.5f,INFINITY,-INFINITY,NAN,0x1p-100f};
   values.fogParam[0][1]=params[(test/4)%8];
   values.fogParam[0][0]=params[test%8];
  }
  for(unsigned i=0;i<192;i++)for(unsigned k=0;k<4;k++)values.c[i][k]=((int)((i*7+k*13)%37)-18)*.125f;
  if(state.is_fixed_function){
   unsigned f=test-3136;
   const float sizes[]={0.0f,-0.0f,1.0f,.25f,-1.0f,2.5f,INFINITY,NAN};
   values.pointSizeReg[0]=sizes[(f/8)%8];values.pointScale[0]=sizes[(f/64)%8];
   if(f&128){
    static const float composite[4][4]={{1,0,0,2},{0,1,0,-3},{0,0,1,.5f},{0,0,0,1}};
    memcpy(values.c,composite,sizeof(composite));
   }
   if(f&256){
    static const float composite[4][4]={{.125f,-.25f,.5f,1},{-.5f,2,.0625f,-1},{1,.125f,-.25f,.5f},{.25f,-.125f,.5f,-2}};
    memcpy(values.c,composite,sizeof(composite));
   }
   values.c[NV_IGRAPH_XF_XFCTX_VPOFF][0]=-2.375f;
   values.c[NV_IGRAPH_XF_XFCTX_VPOFF][1]=1.1875f;
   values.c[NV_IGRAPH_XF_XFCTX_VPOFF][2]=INFINITY;
   values.c[NV_IGRAPH_XF_XFCTX_VPOFF][3]=NAN;
   if(state.fog_enable){
    const float params[]={-.01f,0.0f,-0.0f,.5f,INFINITY,-INFINITY,NAN,0x1p-100f};
    values.fogCtl[0]=(f*13u)&127u; /* Must not select static fog or foggen. */
    values.fogParam[0][0]=params[(f/5)%8];values.fogParam[0][1]=params[(f/40)%8];
    values.c[NV_IGRAPH_XF_XFCTX_FOG][0]=.25f;
    values.c[NV_IGRAPH_XF_XFCTX_FOG][1]=-.5f;
    values.c[NV_IGRAPH_XF_XFCTX_FOG][2]=.125f;
    values.c[NV_IGRAPH_XF_XFCTX_FOG][3]=params[(f/80)%8];
   }
  }
  if(state.is_fixed_function && state.point_params_enable){
   const float defaults[]={1,.125f,.0625f,4,17,-23,.25f,1};
   const float samples[]={0.0f,-0.0f,1,-1,INFINITY,-INFINITY,NAN,0x1p-100f};
   for(unsigned k=0;k<8;k++)values.pointParams[k]=defaults[k];
   unsigned f=(test-11536)/16;
   values.pointParams[(f/8)%8]=samples[f%8];
  }
  static const float skin_weights[7][4]={
   {.1f,.2f,.3f,.4f},{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1},
   {-.25f,1.5f,-2,4},{.5f,.5f,0,0}
  };
  if(test>=15120){
   unsigned f=test-15120;
   values.fogCtl[0]=((f>>10)&1u) | (((f>>4)&7u)<<1) | (((f>>7)&7u)<<4) | 0xa5a50000u;
   values.texMatEnable[0]=(f&15u)|0xffff0000u;
   values.c[NV_IGRAPH_XF_XFCTX_FOG][0]=.25f;
   values.c[NV_IGRAPH_XF_XFCTX_FOG][1]=-.5f;
   values.c[NV_IGRAPH_XF_XFCTX_FOG][2]=.125f;
   values.c[NV_IGRAPH_XF_XFCTX_FOG][3]=.5f;
   if(f&2048){
    const float params[]={-.01f,0.0f,-0.0f,.5f,INFINITY,-INFINITY,NAN,0x1p-100f};
    values.fogParam[0][0]=params[(f>>7)&7];values.fogParam[0][1]=params[(f>>4)&7];
    values.c[NV_IGRAPH_XF_XFCTX_FOG][3]=params[(f>>7)&7];
   }else{values.fogParam[0][0]=1.25f;values.fogParam[0][1]=-.01f;}
  }
  if(test>=19216){
   unsigned f=test-19216;
   const float params[]={-.01f,0.0f,-0.0f,.5f,INFINITY,-INFINITY,NAN,0x1p-100f};
   values.fogParam[0][0]=1.25f;values.fogParam[0][1]=params[(f/16)%8];
   values.c[NV_IGRAPH_XF_XFCTX_FOG][3]=params[(f/16+3)%8];
  }
  if(test>=19984){
   unsigned f=test-19984;
   for(unsigned i=0;i<NV2A_LTCTXA_COUNT;i++)for(unsigned k=0;k<4;k++)values.ltctxa[i][k]=((int)((i*3+k*5)%13)-6)*.125f;
   for(unsigned i=0;i<NV2A_LTCTXB_COUNT;i++)for(unsigned k=0;k<4;k++)values.ltctxb[i][k]=((int)((i*7+k*3)%17)-8)*.0625f;
   /* Keep specular cutoffs observable instead of saturating every color. */
   for(unsigned i=0;i<NV2A_LTCTXB_COUNT;i++)for(unsigned k=0;k<3;k++){
    if((f&128) && (i%6==2 || i%6==5))values.ltctxb[i][k]*=0.0001f;
    if((f&64) && (i%6==1 || i%6==4))values.ltctxb[i][k]*=.01f;
   }
   values.material_alpha[0]=.625f;values.back_material_alpha[0]=.375f;
   for(unsigned i=0;i<8;i++)for(unsigned k=0;k<3;k++){
    values.lightInfiniteDirection[i][k]=.2f+(i%3)*.125f+k*.17f;
    values.lightInfiniteHalfVector[i][k]=-.5f+i*.125f+k*.5f;
   }
   static const float coefficients[8][3]={{-.25f,.75f,.125f},{1,0,0},{0,1,-.5f},{NAN,1,1},{0,INFINITY,1},{0,1,INFINITY},{0,0,-0.0f},{0,0,-1}};
   for(unsigned k=0;k<3;k++){values.specularParams[0][k]=coefficients[(f/64)%8][k];values.specularParamsBack[0][k]=coefficients[(f/128+3)%8][k];}
   values.c[NV_IGRAPH_XF_XFCTX_EYEP][0]=.125f;values.c[NV_IGRAPH_XF_XFCTX_EYEP][1]=-.5f;values.c[NV_IGRAPH_XF_XFCTX_EYEP][2]=1.75f;values.c[NV_IGRAPH_XF_XFCTX_EYEP][3]=f&16?1:2;
  }
  if(test>=24080){
   unsigned f=test-24080;
   static const float identity[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
   for(unsigned b=0;b<4;b++){
    memcpy(values.c+NV_IGRAPH_XF_XFCTX_MMAT0+b*8,identity,sizeof(identity));
    memcpy(values.c+NV_IGRAPH_XF_XFCTX_IMMAT0+b*8,identity,sizeof(identity));
   }
   const float ranges[]={0,1,0x1.fffffep-1f,2,INFINITY,-1,NAN,1000000};
   static const float attenuation[8][3]={{1,0,0},{.5f,.125f,0},{.25f,.125f,.0625f},{0,0,0},{-1,0,0},{INFINITY,0,0},{1,NAN,0},{1,0,0x1p-100f}};
   for(unsigned l=0;l<8;l++){
    unsigned range_style=(f/256+l)%8, style=(f/512+l)%8;
    values.ltc1[NV_IGRAPH_XF_LTC1_r0+l][0]=ranges[range_style];
    unsigned attenuation_style=range_style;
    if(f&128){if(range_style==4)attenuation_style=6;if(range_style==7)attenuation_style=5;}
    for(unsigned k=0;k<3;k++)values.lightLocalAttenuation[l][k]=attenuation[attenuation_style][k];
    values.lightLocalPosition[l][0]=40.125f+l*.25f;values.lightLocalPosition[l][1]=40-l*.125f;values.lightLocalPosition[l][2]=101+l*.0625f;
    float *spot=values.ltctxa[NV_IGRAPH_XF_LTCTXA_L0_SPT+l*2], *coeff=values.ltctxa[NV_IGRAPH_XF_LTCTXA_L0_K+l*2];
    spot[0]=spot[1]=0;spot[2]=1;spot[3]=0;
    coeff[0]=0;coeff[1]=1;coeff[2]=0;
    if(style==1)spot[3]=-1;
    if(style==2){spot[3]=-.5f;coeff[0]=.125f;coeff[1]=.5f;coeff[2]=.75f;}
    if(style==3){spot[2]=2e19f;spot[3]=-1e19f;}
    if(style>=4){spot[2]=0;spot[3]=.5f;}
    if(style==4 && (f&64))coeff[1]=NAN;
    if(style==5)coeff[0]=-.75f;
    if(style==6)coeff[1]=0;
    if(style==7){coeff[1]=0;coeff[2]=-1;}
   }
  }
  unsigned packed=0;for(unsigned a=0;a<16;a++)if(state.uniform_attrs&(1u<<a)) {
   for(unsigned k=0;k<4;k++)values.inlineValue[packed][k]=a==0?(k==3?1:k==2?100:40):(a==15?2:((int)(a+k)%7-3)*.5f);
   if(state.is_fixed_function && state.fixed_function.skinning!=SKINNING_OFF && a==1)for(unsigned k=0;k<4;k++)values.inlineValue[packed][k]=skin_weights[(test/16)%7][k];
   if(test>=39440 && test<40464 && a==15)for(unsigned k=0;k<4;k++)values.inlineValue[packed][k]=-96.0f+((test-39440)/192)%32;
   packed++;
  }
  const float samples[]={0,-0.0f,1,-1,2,-2,.5f,-.5f,4,8,16,-32,INFINITY,-INFINITY,NAN,0x1p-100f};
  for(unsigned v=0;v<VERTICES;v++)for(unsigned a=0;a<16;a++)for(unsigned k=0;k<4;k++) {
   float f=samples[(v+a*3+k*5)%16];if(a==0 && !(state.is_fixed_function && (test&512)))f=k==3?1:k==2?100:(float)(v*3+40);if(a==15)f=(float)(v%8);if(test>=39440 && test<40464 && a==15){f=-96.0f+(v%32);int effective=192+(int)((test-39440)%64)+(int)floorf(f+.001f);assert(effective>=0 && effective<192);}
   if(test>=24080 && a==0 && !(state.compressed_attrs&1) && v%8<7)f=k==3?1:k==2?100:k==1?40:40+v*.125f;
   if(state.is_fixed_function && state.fixed_function.skinning!=SKINNING_OFF && a==1 && !(state.compressed_attrs&2) && v%8<7)f=skin_weights[v%8][k];
   memcpy(input.mapped+v*256+a*16+k*4,&f,4);
  }
  for(unsigned path=0;path<2;path++) {
   pack(uniform.mapped,&modules[path].uniforms,&values);memset(output.mapped,0xa5,VERTICES*240);
   assert(prepared_hash==fixture_key_hash(&state,&opts));
   size_t words;GlslcResult reference_module=modules[path];
   if(!path && (state.point_params_enable || state.is_fixed_function))reference_module.words=preserve_float(&modules[path],&reference_module.num_words);
   uint32_t *code=xfb(&reference_module,&words);
   if(reference_module.words!=modules[path].words)free(reference_module.words);
   VkShaderModuleCreateInfo smi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=words*4,.pCode=code};VkShaderModule shader;CHECK(vkCreateShaderModule(device,&smi,NULL,&shader));free(code);
   VkPipelineShaderStageCreateInfo stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=shader,.pName="main"};
   if(!pipeline_cache){VkPipelineCacheCreateInfo ci={.sType=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};CHECK(vkCreatePipelineCache(device,&ci,NULL,&pipeline_cache));}
   VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=1,.pStages=&stage,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pRasterizationState=&rs,.pMultisampleState=&ms,.layout=layout,.renderPass=pass};VkPipeline pipeline;CHECK(vkCreateGraphicsPipelines(device,pipeline_cache,1,&gp,NULL,&pipeline));
   VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&bi));VkRenderPassBeginInfo rb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffer,.renderArea.extent={1,1}};
   vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);
   VkDeviceSize zero=0;vkCmdBindVertexBuffers(cmd,0,1,&input.handle,&zero);
   if(modules[path].push_constants.count){uint8_t push[256];pack(push,&modules[path].push_constants,&values);vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT,0,(uint32_t)modules[path].push_constants.size,push);}
   bind(cmd,0,1,&output.handle,&zero,NULL);begin(cmd,0,0,NULL,NULL);vkCmdDraw(cmd,VERTICES,1,0,0);end(cmd,0,0,NULL,NULL);vkCmdEndRenderPass(cmd);
   VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=output.handle,.size=VK_WHOLE_SIZE};
   vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);CHECK(vkEndCommandBuffer(cmd));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fi,NULL,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
   if(test>=39440 && test<40464 && ((test-39440)/64)%3!=1)for(unsigned v=0;v<VERTICES;v++){
    int a0=state.uniform_attrs&(1u<<15)?-96+(int)(((test-39440)/192)%32):-96+(int)(v%32);
    int effective=192+(int)((test-39440)%64)+a0;assert(effective>=0 && effective<192);
    for(unsigned k=0;k<4;k++){uint32_t got,expected;memcpy(&got,output.mapped+v*240+5*16+k*4,4);memcpy(&expected,&values.c[effective][k],4);assert(got==expected);constant_checks++;}
   }
   if(!path)memcpy(reference,output.mapped,sizeof(reference));else for(unsigned v=0;v<VERTICES;v++)for(unsigned a=0;a<15;a++)for(unsigned k=0;k<(a==4||a==12||a==14?1u:4u);k++) {
    unsigned index=v*60+a*4+k;uint32_t got;memcpy(&got,output.mapped+index*4,4);float x,y;memcpy(&x,&reference[index],4);memcpy(&y,&got,4);compared++;
    if(got!=reference[index] && !(isnan(x)&&isnan(y))){if(failures++<25)fprintf(stderr,"case %u MAC %u ILU %u vertex %u output %u.%u: %08x (%g) != %08x (%g)\n",test,m,l,v,a,k,reference[index],x,got,y);}
   }
   vkDestroyFence(device,fence,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,shader,NULL);CHECK(vkResetCommandBuffer(cmd,0));
  }
  release(&modules[0]);release(&modules[1]);
 }
 drop(input);drop(output);drop(uniform);vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,pass,NULL);vkDestroyDescriptorPool(device,descriptor_pool,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);vkDestroyCommandPool(device,pool,NULL);if(pipeline_cache)vkDestroyPipelineCache(device,pipeline_cache,NULL);vkDestroyDevice(device,NULL);if(validated)destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
 printf("Independent relative constants: %u exact output checks\n",constant_checks);
 printf("Translation CPU clock: GLSL %.3f ms, direct %.3f ms; emitted words GLSL=%zu direct=%zu (work counts; not frame latency)\n",1000.0*reference_ticks/CLOCKS_PER_SEC,1000.0*direct_ticks/CLOCKS_PER_SEC,reference_words,direct_words);
 printf("Direct VSH Vulkan: %u scalar results, %u differences, %u validation errors; validation=%s\n",compared,failures,errors,validated?"enabled":"DISABLED");return failures||errors?1:0;
}

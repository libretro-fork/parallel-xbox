/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only instrumentation: copy each GS output to a storage buffer before
 * EmitVertex. This adds no storage writes or readback to the renderer. */
#include <volk.h>
#include <spirv/unified1/spirv.h>
#define VCHECK(x) do {VkResult result_=(x);if(result_!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#x,result_);abort();}}while(0)
#define GEOM_CASES 32
static unsigned geometry_vertices = 2, geometry_outputs = 2;
#define RECORD_SLOTS 15
#define RECORD_FLOATS (RECORD_SLOTS*4)
typedef struct {uint32_t *words;size_t n,cap;uint32_t bound;} Capture;
typedef struct {uint32_t op,type,child,storage,width,count,location,position,point;} CaptureInfo;
static void capture_word(Capture*c,uint32_t x){if(c->n==c->cap){c->cap=c->cap?c->cap*2:2048;c->words=realloc(c->words,c->cap*4);assert(c->words);}c->words[c->n++]=x;}
static uint32_t capture_id(Capture*c){return c->bound++;}
static void capture_op(Capture*c,SpvOp op,unsigned n,...){capture_word(c,((n+1)<<16)|op);va_list a;va_start(a,n);for(unsigned i=0;i<n;i++)capture_word(c,va_arg(a,uint32_t));va_end(a);}
static uint32_t capture_value(Capture*c,SpvOp op,uint32_t type,unsigned n,...){uint32_t id=capture_id(c);capture_word(c,((n+3)<<16)|op);capture_word(c,type);capture_word(c,id);va_list a;va_start(a,n);for(unsigned i=0;i<n;i++)capture_word(c,va_arg(a,uint32_t));va_end(a);return id;}
static Capture instrument(const uint32_t *words,size_t count){
 uint32_t existing_primitive=0;uint32_t bound=words[3];CaptureInfo*info=calloc(bound,sizeof(*info));assert(info);
 for(size_t at=5;at<count;){unsigned n=words[at]>>16,op=words[at]&65535;assert(n&&at+n<=count);const uint32_t*p=words+at;
  if(op==SpvOpTypeFloat||op==SpvOpTypeInt){info[p[1]].op=op;info[p[1]].width=p[2];if(op==SpvOpTypeInt)info[p[1]].storage=p[3];}
  if(op==SpvOpTypeVector){info[p[1]].op=op;info[p[1]].child=p[2];info[p[1]].count=p[3];}
  if(op==SpvOpTypePointer){info[p[1]].op=op;info[p[1]].storage=p[2];info[p[1]].child=p[3];}
  if(op==SpvOpVariable){info[p[2]].op=op;info[p[2]].type=p[1];info[p[2]].storage=p[3];}
  if(op==SpvOpDecorate&&p[2]==SpvDecorationLocation)info[p[1]].location=p[3]+1;
  if(op==SpvOpDecorate&&p[2]==SpvDecorationBuiltIn&&p[3]==SpvBuiltInPrimitiveId)existing_primitive=p[1];
  if(op==SpvOpMemberDecorate&&p[3]==SpvDecorationBuiltIn){if(p[4]==SpvBuiltInPosition)info[p[1]].position=p[2]+1;if(p[4]==SpvBuiltInPointSize)info[p[1]].point=p[2]+1;}
  at+=n;
 }
 uint32_t ft=0,ut=0,vt=0,fp=0,vp=0,ip=0,pp=0,sp=0,glout=0,pos_member=0,point_member=0,outputs[13]={0};
 for(unsigned i=1;i<bound;i++){if(info[i].op==SpvOpTypeFloat&&info[i].width==32)ft=i;if(info[i].op==SpvOpTypeInt&&info[i].width==32&&!info[i].storage)ut=i;}
 assert(ft&&ut);
 for(unsigned i=1;i<bound;i++)if(info[i].op==SpvOpTypeVector&&info[i].child==ft&&info[i].count==4)vt=i;
 assert(vt);
 for(unsigned i=1;i<bound;i++){
  if(info[i].op==SpvOpTypePointer){if(info[i].storage==SpvStorageClassOutput){if(info[i].child==ft)fp=i;if(info[i].child==vt)vp=i;}if(info[i].storage==SpvStorageClassInput&&info[i].child==ut)ip=i;if(info[i].storage==SpvStorageClassPrivate&&info[i].child==ut)pp=i;if(info[i].storage==SpvStorageClassStorageBuffer&&info[i].child==vt)sp=i;}
  if(info[i].op==SpvOpVariable&&info[i].storage==SpvStorageClassOutput){unsigned t=info[info[i].type].child;if(info[t].position){glout=i;pos_member=info[t].position-1;point_member=info[t].point-1;}else if(info[i].location&&info[i].location<=13)outputs[info[i].location-1]=i;}
 }
 assert(glout&&vp&&fp&&pos_member<16&&point_member<16);for(unsigned i=0;i<13;i++)assert(outputs[i]);
 Capture c={.bound=bound};uint32_t array=capture_id(&c),block=capture_id(&c),bp=capture_id(&c);bool new_ip=!ip,new_pp=!pp,new_sp=!sp;
 if(!ip)ip=capture_id(&c);if(!pp)pp=capture_id(&c);if(!sp)sp=capture_id(&c);
 uint32_t buffer=capture_id(&c),counter=capture_id(&c),primitive=existing_primitive?existing_primitive:capture_id(&c),u[16],fz=capture_id(&c);
 for(unsigned i=0;i<16;i++)u[i]=capture_id(&c);
 for(unsigned i=0;i<5;i++)capture_word(&c,words[i]);bool annotated=false,declared=false;
 for(size_t at=5;at<count;){unsigned n=words[at]>>16,op=words[at]&65535;const uint32_t*p=words+at;
  if(!annotated&&op>=SpvOpTypeVoid&&op<=SpvOpTypeForwardPointer){
   capture_op(&c,SpvOpDecorate,3,array,SpvDecorationArrayStride,16u);capture_op(&c,SpvOpDecorate,2,block,SpvDecorationBlock);capture_op(&c,SpvOpMemberDecorate,4,block,0u,SpvDecorationOffset,0u);
   capture_op(&c,SpvOpDecorate,3,buffer,SpvDecorationDescriptorSet,0u);capture_op(&c,SpvOpDecorate,3,buffer,SpvDecorationBinding,1u);if(!existing_primitive)capture_op(&c,SpvOpDecorate,3,primitive,SpvDecorationBuiltIn,SpvBuiltInPrimitiveId);annotated=true;
  }
  if(op==SpvOpFunction&&!declared){
   capture_op(&c,SpvOpTypeRuntimeArray,2,array,vt);capture_op(&c,SpvOpTypeStruct,2,block,array);capture_op(&c,SpvOpTypePointer,3,bp,SpvStorageClassStorageBuffer,block);
   if(new_ip)capture_op(&c,SpvOpTypePointer,3,ip,SpvStorageClassInput,ut);if(new_pp)capture_op(&c,SpvOpTypePointer,3,pp,SpvStorageClassPrivate,ut);if(new_sp)capture_op(&c,SpvOpTypePointer,3,sp,SpvStorageClassStorageBuffer,vt);
   for(unsigned i=0;i<16;i++)capture_op(&c,SpvOpConstant,3,ut,u[i],i);capture_op(&c,SpvOpConstant,3,ft,fz,0u);
   capture_op(&c,SpvOpVariable,3,bp,buffer,SpvStorageClassStorageBuffer);capture_op(&c,SpvOpVariable,4,pp,counter,SpvStorageClassPrivate,u[0]);if(!existing_primitive)capture_op(&c,SpvOpVariable,3,ip,primitive,SpvStorageClassInput);declared=true;
  }
  if(op==SpvOpEmitVertex){
   uint32_t primitive_type=existing_primitive?info[info[primitive].type].child:ut;
   uint32_t prim=capture_value(&c,SpvOpLoad,primitive_type,1,primitive);if(primitive_type!=ut)prim=capture_value(&c,SpvOpBitcast,ut,1,prim);
   uint32_t index=capture_value(&c,SpvOpLoad,ut,1,counter);
   uint32_t base=capture_value(&c,SpvOpIMul,ut,2,prim,u[geometry_outputs]);base=capture_value(&c,SpvOpIAdd,ut,2,base,index);base=capture_value(&c,SpvOpIMul,ut,2,base,u[15]);
   for(unsigned slot=0;slot<15;slot++){
    bool scalar=slot==4||slot==12||slot==14;uint32_t src;
    if(slot<13)src=outputs[slot];else src=capture_value(&c,SpvOpAccessChain,scalar?fp:vp,2,glout,u[slot==13?pos_member:point_member]);
    uint32_t val=capture_value(&c,SpvOpLoad,scalar?ft:vt,1,src);if(scalar)val=capture_value(&c,SpvOpCompositeConstruct,vt,4,val,fz,fz,fz);
    uint32_t dst_index=capture_value(&c,SpvOpIAdd,ut,2,base,u[slot]);uint32_t dst=capture_value(&c,SpvOpAccessChain,sp,3,buffer,u[0],dst_index);capture_op(&c,SpvOpStore,2,dst,val);
   }
   index=capture_value(&c,SpvOpIAdd,ut,2,index,u[1]);capture_op(&c,SpvOpStore,2,counter,index);
  }
  if(op==SpvOpEntryPoint){capture_word(&c,((n+(existing_primitive?2:3))<<16)|op);for(unsigned i=1;i<n;i++)capture_word(&c,p[i]);capture_word(&c,buffer);capture_word(&c,counter);if(!existing_primitive)capture_word(&c,primitive);}
  else for(unsigned i=0;i<n;i++)capture_word(&c,p[i]);at+=n;
 }
 assert(annotated&&declared);c.words[3]=c.bound;free(info);return c;
}

typedef struct {float position[4],attr[10][4];} TestVertex;
typedef struct {VkBuffer buffer;VkDeviceMemory memory;void*mapped;} TestBuffer;
static VkInstance gi;static VkPhysicalDevice gpu;static VkDevice gd;static VkQueue queue;static VkCommandPool pool;static VkCommandBuffer cmd;static VkRenderPass pass;static VkFramebuffer fb;static VkDescriptorSetLayout dsl;static VkPipelineLayout layout;static VkDescriptorPool dp;static VkDescriptorSet descriptors;static VkFence fence;static TestBuffer input,output;static VkPhysicalDeviceMemoryProperties mem;static unsigned validation_errors,gpu_cases;static VkDebugUtilsMessengerEXT messenger;static bool validation_enabled;
static TestVertex vertices[GEOM_CASES*4];
static VKAPI_ATTR VkBool32 VKAPI_CALL gpu_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user){(void)type;(void)user;if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){validation_errors++;fprintf(stderr,"%s\n",data->pMessage);}return VK_FALSE;}
static TestBuffer make_buffer(VkDeviceSize size){
 TestBuffer b={0};VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};VCHECK(vkCreateBuffer(gd,&ci,NULL,&b.buffer));VkMemoryRequirements mr;vkGetBufferMemoryRequirements(gd,b.buffer,&mr);unsigned t=0;while(t<mem.memoryTypeCount&&(!(mr.memoryTypeBits&(1u<<t))||(mem.memoryTypes[t].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))!=(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))t++;assert(t<mem.memoryTypeCount);VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=mr.size,.memoryTypeIndex=t};VCHECK(vkAllocateMemory(gd,&ai,NULL,&b.memory));VCHECK(vkBindBufferMemory(gd,b.buffer,b.memory,0));VCHECK(vkMapMemory(gd,b.memory,0,VK_WHOLE_SIZE,0,&b.mapped));return b;
}
static void init_gpu(void){
 VCHECK(volkInitialize());
 uint32_t nl=0;VCHECK(vkEnumerateInstanceLayerProperties(&nl,NULL));VkLayerProperties*layers=calloc(nl?nl:1,sizeof(*layers));VCHECK(vkEnumerateInstanceLayerProperties(&nl,layers));for(unsigned i=0;i<nl;i++)if(!strcmp(layers[i].layerName,"VK_LAYER_KHRONOS_validation"))validation_enabled=true;free(layers);
 const char*layer="VK_LAYER_KHRONOS_validation",*extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledLayerCount=validation_enabled,.ppEnabledLayerNames=&layer,.enabledExtensionCount=validation_enabled,.ppEnabledExtensionNames=&extension};VCHECK(vkCreateInstance(&ic,NULL,&gi));volkLoadInstance(gi);
 if(validation_enabled){PFN_vkCreateDebugUtilsMessengerEXT create=(void*)vkGetInstanceProcAddr(gi,"vkCreateDebugUtilsMessengerEXT");assert(create);VkDebugUtilsMessengerCreateInfoEXT mi={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=7,.pfnUserCallback=gpu_message};VCHECK(create(gi,&mi,NULL,&messenger));}
 uint32_t n=1;VCHECK(vkEnumeratePhysicalDevices(gi,&n,&gpu));VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(gpu,&props);assert(props.apiVersion>=VK_API_VERSION_1_3);printf("GPU geometry comparison: %s; validation=%u\n",props.deviceName,validation_enabled);VkPhysicalDeviceFeatures features;vkGetPhysicalDeviceFeatures(gpu,&features);assert(features.geometryShader&&features.vertexPipelineStoresAndAtomics&&features.shaderTessellationAndGeometryPointSize);vkGetPhysicalDeviceMemoryProperties(gpu,&mem);
 uint32_t nq=32;VkQueueFamilyProperties families[32];vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,families);unsigned family=0;while(family<nq&&!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;assert(family<nq);float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};VkPhysicalDeviceFeatures enable={.geometryShader=true,.vertexPipelineStoresAndAtomics=true,.shaderTessellationAndGeometryPointSize=true};VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.pEnabledFeatures=&enable};VCHECK(vkCreateDevice(gpu,&dc,NULL,&gd));volkLoadDevice(gd);vkGetDeviceQueue(gd,family,0,&queue);
 VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};VCHECK(vkCreateCommandPool(gd,&pi,NULL,&pool));VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VCHECK(vkAllocateCommandBuffers(gd,&ca,&cmd));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VCHECK(vkCreateFence(gd,&fi,NULL,&fence));
 VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.subpassCount=1};VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};rp.pSubpasses=&sub;VCHECK(vkCreateRenderPass(gd,&rp,NULL,&pass));VkFramebufferCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.width=1,.height=1,.layers=1};VCHECK(vkCreateFramebuffer(gd,&fc,NULL,&fb));
 VkDescriptorSetLayoutBinding bindings[2]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT},{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_GEOMETRY_BIT}};VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};VCHECK(vkCreateDescriptorSetLayout(gd,&di,NULL,&dsl));VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&dsl};VCHECK(vkCreatePipelineLayout(gd,&li,NULL,&layout));VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};VkDescriptorPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};VCHECK(vkCreateDescriptorPool(gd,&pc,NULL,&dp));VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=dp,.descriptorSetCount=1,.pSetLayouts=&dsl};VCHECK(vkAllocateDescriptorSets(gd,&da,&descriptors));
 input=make_buffer(sizeof(vertices));output=make_buffer(GEOM_CASES*8*RECORD_FLOATS*sizeof(float));VkDescriptorBufferInfo buffers[2]={{input.buffer,0,VK_WHOLE_SIZE},{output.buffer,0,VK_WHOLE_SIZE}};VkWriteDescriptorSet writes[2];for(unsigned i=0;i<2;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=descriptors,.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buffers[i]};vkUpdateDescriptorSets(gd,2,writes,0,NULL);
 for(unsigned i=0;i<GEOM_CASES*4;i++){TestVertex*v=&vertices[i];v->position[0]=(i&1)?0.75f:-0.75f;v->position[1]=(float)(i%7)*0.125f;v->position[2]=0.25f;v->position[3]=1;for(unsigned a=0;a<10;a++)for(unsigned c=0;c<4;c++)v->attr[a][c]=(float)((int)(i*17+a*7+c*3)%127-63)*0.25f;v->attr[4][0]=(float)(i%4+1);}
 /* Zero-length, signed zero, infinities, NaNs and large finite deltas. */
 vertices[2].attr[9][0]=vertices[3].attr[9][0]=0;vertices[2].attr[9][1]=vertices[3].attr[9][1]=-0.0f;vertices[4].attr[9][0]=INFINITY;vertices[5].attr[9][0]=INFINITY;vertices[6].attr[9][1]=NAN;vertices[8].attr[9][0]=1e30f;vertices[9].attr[9][0]=-1e30f;memcpy(input.mapped,vertices,sizeof(vertices));
}
static VkShaderModule gpu_module(const uint32_t*words,size_t n){VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=n*4,.pCode=words};VkShaderModule m;VCHECK(vkCreateShaderModule(gd,&ci,NULL,&m));return m;}
static GlslcResult test_vertex_shader(bool smooth,bool version){
 MString*s=mstring_from_fmt("#version 450\n");pgraph_glsl_get_vtx_header(s,true,smooth,false,true,false);mstring_append(s,"layout(std430,binding=0) readonly buffer Input {vec4 v[];};\nvoid main(){int i=gl_VertexIndex*11;gl_Position=v[i];gl_PointSize=v[i+5].x;\n");const char*names[]={"vtxD0","vtxD1","vtxB0","vtxB1","vtxFog","vtxT0","vtxT1","vtxT2","vtxT3","vtxPos0","vtxPos1","vtxPos2","triMZ"};for(unsigned i=0;i<13;i++){if(i==4)mstring_append_fmt(s,"v_%s=v[i+5].y;\n",names[i]);else if(i==12)mstring_append_fmt(s,"v_%s=0.0;\n",names[i]);else mstring_append_fmt(s,"v_%s=v[i+%u];\n",names[i],i<10?i+1:10);}mstring_append(s,"}\n");GlslcResult m=compile(GLSLC_STAGE_VERTEX,s->str,version);mstring_unref(s);return m;
}
static void run_geometry(VkShaderModule vs,VkShaderModule gs,float*result){
 VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_GEOMETRY_BIT,.module=gs,.pName="main"}};VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=geometry_vertices==4?VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:geometry_vertices==3?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:VK_PRIMITIVE_TOPOLOGY_LINE_LIST};VkViewport viewport={0,0,1,1,0,1};VkRect2D scissor={{0,0},{1,1}};VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.rasterizerDiscardEnable=true,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&cb,.layout=layout,.renderPass=pass};VkPipeline pipeline;VCHECK(vkCreateGraphicsPipelines(gd,VK_NULL_HANDLE,1,&gp,NULL,&pipeline));memset(output.mapped,0xcd,GEOM_CASES*geometry_outputs*RECORD_FLOATS*sizeof(float));VCHECK(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VCHECK(vkBeginCommandBuffer(cmd,&bi));VkRenderPassBeginInfo rb={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,.renderArea={{0,0},{1,1}}};vkCmdBeginRenderPass(cmd,&rb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&descriptors,0,NULL);vkCmdDraw(cmd,GEOM_CASES*geometry_vertices,1,0,0);vkCmdEndRenderPass(cmd);VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=output.buffer,.size=VK_WHOLE_SIZE};vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);VCHECK(vkEndCommandBuffer(cmd));VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};VCHECK(vkQueueSubmit(queue,1,&si,fence));VCHECK(vkWaitForFences(gd,1,&fence,true,10000000000ull));VCHECK(vkResetFences(gd,1,&fence));memcpy(result,output.mapped,GEOM_CASES*geometry_outputs*RECORD_FLOATS*sizeof(float));vkDestroyPipeline(gd,pipeline,NULL);
}
static bool same_float(float a,float b){return (isnan(a)&&isnan(b))||!memcmp(&a,&b,sizeof(a));}
static void compare_on_gpu(const char*folder,unsigned version,unsigned smooth,unsigned provoking,GlslcResult*reference,SpvDirectModule*direct){
 if(!gd)init_gpu();Capture r=instrument(reference->words,reference->num_words),d=instrument(direct->words,direct->num_words);save(folder,"capture-reference",version,smooth,provoking,r.words,r.n);save(folder,"capture-direct",version,smooth,provoking,d.words,d.n);GlslcResult vertex=test_vertex_shader(smooth,version);save(folder,"vertex",version,smooth,provoking,vertex.words,vertex.num_words);VkShaderModule vs=gpu_module(vertex.words,vertex.num_words),rs=gpu_module(r.words,r.n),ds=gpu_module(d.words,d.n);float expected[GEOM_CASES*2*RECORD_FLOATS],actual[GEOM_CASES*2*RECORD_FLOATS];run_geometry(vs,rs,expected);run_geometry(vs,ds,actual);
 for(unsigned i=0;i<ARRAY_SIZE(actual);i++)if(!same_float(expected[i],actual[i])){fprintf(stderr,"GPU mismatch version=%u smooth=%u provoking=%u float=%u ref=%a direct=%a\n",version,smooth,provoking,i,expected[i],actual[i]);abort();}
 /* Independent oracle for every copied value and the synthetic third vertex. */
 for(unsigned line=0;line<GEOM_CASES;line++)for(unsigned v=0;v<2;v++){
  float oracle[RECORD_FLOATS]={0};TestVertex*cur=&vertices[line*2+v];unsigned colour=smooth?v:provoking;
  for(unsigned slot=0;slot<9;slot++){if(slot==4)oracle[slot*4]=cur->attr[4][1];else memcpy(oracle+slot*4,vertices[line*2+(slot<4?colour:v)].attr[slot],16);}
  memcpy(oracle+9*4,vertices[line*2].attr[9],16);memcpy(oracle+10*4,vertices[line*2+1].attr[9],16);float*p0=vertices[line*2].attr[9],*p1=vertices[line*2+1].attr[9];oracle[11*4]=-(p1[1]-p0[1])+p0[0];oracle[11*4+1]=(p1[0]-p0[0])+p0[1];oracle[11*4+2]=p0[2];oracle[11*4+3]=p0[3];memcpy(oracle+13*4,cur->position,16);oracle[14*4]=cur->attr[4][0];
  for(unsigned f=0;f<RECORD_FLOATS;f++){float got=actual[(line*2+v)*RECORD_FLOATS+f];if(!same_float(got,oracle[f])&&!(got==0&&oracle[f]==0)){fprintf(stderr,"CPU oracle mismatch line=%u vertex=%u field=%u got=%a expected=%a\n",line,v,f,got,oracle[f]);abort();}}
 }
 assert(!validation_errors);printf("GPU geometry: SPV %u smooth=%u provoking=%u, %u full vertex records match reference and CPU oracle\n",version,smooth,provoking,GEOM_CASES*2);gpu_cases++;vkDestroyShaderModule(gd,vs,NULL);vkDestroyShaderModule(gd,rs,NULL);vkDestroyShaderModule(gd,ds,NULL);free(vertex.words);free(r.words);free(d.words);
}
static void finish_gpu(void){assert(gpu_cases>=8&&!validation_errors);printf("GPU geometry: %u shader pairs passed; validation layer %s\n",gpu_cases,validation_enabled?"enabled, zero errors":"unavailable (SPIR-V checked separately)");vkUnmapMemory(gd,input.memory);vkUnmapMemory(gd,output.memory);vkDestroyBuffer(gd,input.buffer,NULL);vkDestroyBuffer(gd,output.buffer,NULL);vkFreeMemory(gd,input.memory,NULL);vkFreeMemory(gd,output.memory,NULL);vkDestroyDescriptorPool(gd,dp,NULL);vkDestroyPipelineLayout(gd,layout,NULL);vkDestroyDescriptorSetLayout(gd,dsl,NULL);vkDestroyFramebuffer(gd,fb,NULL);vkDestroyRenderPass(gd,pass,NULL);vkDestroyFence(gd,fence,NULL);vkDestroyCommandPool(gd,pool,NULL);vkDestroyDevice(gd,NULL);if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(gi,"vkDestroyDebugUtilsMessengerEXT");destroy(gi,messenger,NULL);}vkDestroyInstance(gi,NULL);gd=VK_NULL_HANDLE;gpu_cases=0;}

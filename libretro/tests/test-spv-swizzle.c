/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
#include "hw/xbox/nv2a/pgraph/spv/swizzle.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "spirv_reflect.h"
#include "swizzle-reference-glsl.h"
/* QUIET ERRORS */
static void save(const char *folder,const char *name,const uint32_t *words,size_t count){char path[1024];snprintf(path,sizeof(path),"%s/%s.spv",folder,name);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(words,4,count,f)==count);assert(!fclose(f));}
static GlslcResult reference(bool to_ram){GlslcResult result;GlslcOptions opts={0};if(!glslc_compile(GLSLC_STAGE_COMPUTE,to_ram?swizzle_glsl:unswizzle_glsl,&opts,&result)){fprintf(stderr,"Reference line %d: %s\n",result.error_line,result.error);abort();}return result;}
#ifdef TEST_VULKAN
#define VK_NO_PROTOTYPES
#include <volk.h>
#include "gpu-common.h"
static VkPipeline pipeline(VkShaderModule module,VkPipelineLayout layout){VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};VkPipeline p;CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,NULL,&p));return p;}
/* Independent coordinate interleaver: advance the source coordinate bits in
 * X/Y/Z order while each dimension still has a bit. */
static uint32_t morton(unsigned x,unsigned y,unsigned z,unsigned width,unsigned height,unsigned depth){uint32_t out=0,lane=1;for(unsigned bit=1;bit<width||bit<height||bit<depth;bit<<=1){if(bit<width){if(x&bit)out|=lane;lane<<=1;}if(bit<height){if(y&bit)out|=lane;lane<<=1;}if(bit<depth){if(z&bit)out|=lane;lane<<=1;}}return out;}
static uint32_t random_word(uint32_t x){x^=x>>16;x*=0x7feb352d;x^=x>>15;x*=0x846ca68b;return x^(x>>16);}
static void check_bytes(const void *got,const void *expected,size_t size,bool to_ram,unsigned bpp,bool pal,unsigned w,unsigned h,unsigned d,unsigned candidate){if(!memcmp(got,expected,size))return;const unsigned char *a=got,*b=expected;for(size_t i=0;i<size;i++)if(a[i]!=b[i]){fprintf(stderr,"Swizzle mismatch ram=%u bpp=%u pal=%u %ux%ux%u candidate=%u byte=%zu got=%02x expected=%02x\n",to_ram,bpp,pal,w,h,d,candidate,i,a[i],b[i]);abort();}}
static double time_kernel(VkPipeline p,VkPipelineLayout layout,VkDescriptorSet set,const uint32_t push[11]){
 VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);if(!props.limits.timestampComputeAndGraphics)return 0;
 uint32_t nq=32;VkQueueFamilyProperties family[32];vkGetPhysicalDeviceQueueFamilyProperties(physical,&nq,family);unsigned q=0;while(q<nq&&!(family[q].queueFlags&VK_QUEUE_GRAPHICS_BIT))q++;if(q==nq||!family[q].timestampValidBits)return 0;unsigned valid_bits=family[q].timestampValidBits;
 VkQueryPoolCreateInfo qi={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=2};VkQueryPool queries;CHECK(vkCreateQueryPool(device,&qi,NULL,&queries));const unsigned repeats=32;begin();vkCmdResetQueryPool(command,queries,0,2);vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,queries,0);for(unsigned i=0;i<repeats;i++){vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,44,push);vkCmdDispatch(command,(push[9]+255)/256,1,1);VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);}vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,queries,1);submit();uint64_t t[2];CHECK(vkGetQueryPoolResults(device,queries,0,2,sizeof(t),t,8,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT));uint64_t elapsed=t[1]-t[0];if(valid_bits<64)elapsed&=(1ull<<valid_bits)-1;vkDestroyQueryPool(device,queries,NULL);return elapsed*props.limits.timestampPeriod/1000.0/repeats;
}
static void gpu_tests(const char *folder){
 gpu_init();VkDescriptorSetLayoutBinding bindings[2];for(unsigned i=0;i<2;i++)bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo di={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};VkDescriptorSetLayout dsl;CHECK(vkCreateDescriptorSetLayout(device,&di,NULL,&dsl));VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,44};VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&dsl,.pushConstantRangeCount=1,.pPushConstantRanges=&range};VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&li,NULL,&layout));VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};VkDescriptorPool dp;CHECK(vkCreateDescriptorPool(device,&dpi,NULL,&dp));VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=dp,.descriptorSetCount=1,.pSetLayouts=&dsl};VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&ai,&set));
 unsigned dims[][3]={{1,1,1},{1,2,1},{2,1,1},{4,1,1},{2,2,1},{8,4,1},{4,8,2},{1,1,8},{32,16,1},{64,64,1},{256,256,1},{1024,512,1},{4,4,16},{1,1024,1},{1024,1,1}};
 for(unsigned to_ram=0;to_ram<2;to_ram++){
  SpvStaticModule m=pgraph_spv_swizzle(to_ram);GlslcResult ref=reference(to_ram);char name[40];snprintf(name,sizeof(name),"reference-%u",to_ram);save(folder,name,ref.words,ref.num_words);VkShaderModule modules[2]={gpu_module(ref.words,ref.num_words),gpu_module(m.words,m.num_words)};free(ref.words);VkPipeline pipelines[2];for(unsigned i=0;i<2;i++){pipelines[i]=pipeline(modules[i],layout);vkDestroyShaderModule(device,modules[i],NULL);}
  for(unsigned dim=0;dim<sizeof(dims)/sizeof(*dims);dim++)for(unsigned bpp=1;bpp<=4;bpp*=2)for(unsigned pal=0;pal<(bpp==1?2u:1u);pal++)for(unsigned alignment=0;alignment<2;alignment++){
   unsigned w=dims[dim][0],h=dims[dim][1],d=dims[dim][2],texels=w*h*d,out_bpp=pal?4:bpp;
   if(to_ram&&(bpp==1||d!=1||(texels*bpp)%4))continue;
   unsigned src=to_ram?16:16+alignment*bpp,dst=32+alignment*16;
   unsigned palette=(src+texels*bpp+19)&~3u;
   size_t vram_size=(to_ram?dst:src)+texels*bpp+128+(pal?1024:0),linear_size=(to_ram?src:dst)+((texels*out_bpp+3)&~3u)+128;
   vram_size=(vram_size+3)&~3u;linear_size=(linear_size+3)&~3u;
   Buffer vram=make_buffer(vram_size,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),linear=make_buffer(linear_size,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
   unsigned char *input=malloc(to_ram?linear_size:vram_size),*expected=malloc(to_ram?vram_size:linear_size);assert(input&&expected);
   size_t input_size=to_ram?linear_size:vram_size,output_size=to_ram?vram_size:linear_size;
   for(size_t i=0;i<input_size;i++)input[i]=(unsigned char)random_word((uint32_t)i+dim*137+alignment*19);
   memset(expected,0xcd,output_size);
   if(!to_ram)memset(expected+dst,0,(texels*out_bpp+3)&~3u);
   for(unsigned z=0;z<d;z++)for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
    unsigned t=(z*h+y)*w+x,sw=morton(x,y,z,w,h,d);if(to_ram)memcpy(expected+dst+sw*bpp,input+src+t*bpp,bpp);else if(pal){unsigned index=input[src+sw];memcpy(expected+dst+t*4,input+palette+index*4,4);}else memcpy(expected+dst+t*bpp,input+src+sw*bpp,bpp);
   }
   uint32_t mx,my,mz;generate_swizzle_masks(w,h,d,&mx,&my,&mz);uint32_t push[11]={src,dst,w,h,d,bpp,mx,my,mz,(texels*out_bpp+3)/4,pal?palette:0};
   VkDescriptorBufferInfo bi[2]={{vram.buffer,0,VK_WHOLE_SIZE},{linear.buffer,0,VK_WHOLE_SIZE}};VkWriteDescriptorSet writes[2];for(unsigned i=0;i<2;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi[i]};vkUpdateDescriptorSets(device,2,writes,0,NULL);
   Buffer source=to_ram?linear:vram,output=to_ram?vram:linear;
   for(unsigned candidate=0;candidate<2;candidate++){
    memcpy(source.mapped,input,input_size);memset(output.mapped,0xcd,output_size);begin();vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[candidate]);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,44,push);vkCmdDispatch(command,(push[9]+255)/256,1,1);host_barrier(output,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);submit();check_bytes(output.mapped,expected,output_size,to_ram,bpp,pal,w,h,d,candidate);assert(!memcmp(source.mapped,input,input_size));gpu_cases++;
   }
   if(dim==11&&!alignment){double times[2][4];for(unsigned sample=0;sample<4;sample++)for(unsigned order=0;order<2;order++){unsigned candidate=order^(sample&1);times[candidate][sample]=time_kernel(pipelines[candidate],layout,set,push);}for(unsigned c=0;c<2;c++)for(unsigned i=0;i<4;i++)for(unsigned j=i+1;j<4;j++)if(times[c][j]<times[c][i]){double t=times[c][i];times[c][i]=times[c][j];times[c][j]=t;}printf("Swizzle GPU timestamps 1024x512 ram=%u bpp=%u palette=%u: original=%.3fus direct=%.3fus (median, four alternating 32-dispatch batches)\n",to_ram,bpp,pal,(times[0][1]+times[0][2])/2,(times[1][1]+times[1][2])/2);}
   free(input);free(expected);drop_buffer(vram);drop_buffer(linear);
  }
  for(unsigned i=0;i<2;i++)vkDestroyPipeline(device,pipelines[i],NULL);
 }
 assert(!errors);printf("Swizzle GPU: %u original/direct/CPU cases passed: 1/2/4-byte, palettes, 3D, partial words, offsets and guards; validation %s\n",gpu_cases,validation_enabled?"enabled":"unavailable");
 vkDestroyDescriptorPool(device,dp,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,dsl,NULL);vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);if(messenger){PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void*)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");destroy(instance,messenger,NULL);}vkDestroyInstance(instance,NULL);
}
#endif
int main(int argc,char **argv){
 quiet_test_errors();setvbuf(stdout,NULL,_IONBF,0);assert(argc==2||argc==3);
 for(unsigned ram=0;ram<2;ram++){
  SpvStaticModule m=pgraph_spv_swizzle(ram);assert(m.num_words&&m.words[0]==0x07230203);for(unsigned i=0;i<10000;i++){SpvStaticModule again=pgraph_spv_swizzle(ram);assert(m.words==again.words&&m.num_words==again.num_words);}
  char name[40];snprintf(name,sizeof(name),"static-%u",ram);save(argv[1],name,m.words,m.num_words);
  SpvReflectShaderModule reflect;assert(spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NO_COPY,m.num_words*4,m.words,&reflect)==SPV_REFLECT_RESULT_SUCCESS);assert(reflect.entry_point_count==1&&reflect.entry_points[0].local_size.x==PGRAPH_SPV_SWIZZLE_WORKGROUP&&reflect.entry_points[0].local_size.y==1&&reflect.entry_points[0].local_size.z==1);assert(reflect.descriptor_binding_count==2&&reflect.push_constant_block_count==1);for(unsigned i=0;i<2;i++){assert(reflect.descriptor_bindings[i].set==0&&reflect.descriptor_bindings[i].binding==i&&reflect.descriptor_bindings[i].descriptor_type==SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER);}assert(reflect.push_constant_blocks[0].member_count==11&&reflect.push_constant_blocks[0].size==44);for(unsigned i=0;i<11;i++)assert(reflect.push_constant_blocks[0].members[i].offset==i*4);spvReflectDestroyShaderModule(&reflect);
 }
 puts("Swizzle static modules: immutable identity and 2-buffer/44-byte layout passed");
#ifdef TEST_VULKAN
 if(argc==3)gpu_tests(argv[1]);
#endif
 return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdlib.h>
#include "vk-query-fixture.h"
#include "query-scope-spv.h"
#define CHECK(x) assert((x)==VK_SUCCESS)
/* PRODUCTION */
#include "vk-query-workload.h"
static void draw_triangle(PGRAPHState *pg) {vkCmdDraw(pg->vk_renderer_state->command_buffer,3,1,0,0);}
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static unsigned errors;
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user) {
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) { errors++;fprintf(stderr,"%s\n",data->pMessage); }
    return VK_FALSE;
}
int main(void) {
    const char *layer="VK_LAYER_KHRONOS_validation";
    const char *extensions[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
    VkValidationFeatureEnableEXT feature=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT vf={.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,.enabledValidationFeatureCount=1,.pEnabledValidationFeatures=&feature};
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pNext=&vf,.pApplicationInfo=&app,
        .enabledLayerCount=1,.ppEnabledLayerNames=&layer,.enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};
    VkInstance instance;CHECK(vkCreateInstance(&ici,NULL,&instance));
    PFN_vkCreateDebugUtilsMessengerEXT create=(void *)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");
    PFN_vkDestroyDebugUtilsMessengerEXT destroy=(void *)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT mi={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,.messageType=7,.pfnUserCallback=validation};
    VkDebugUtilsMessengerEXT messenger;CHECK(create(instance,&mi,NULL,&messenger));
    uint32_t n=1;VkPhysicalDevice gpu;CHECK(vkEnumeratePhysicalDevices(instance,&n,&gpu));
    vkGetPhysicalDeviceMemoryProperties(gpu,&memory_properties);VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu,&props);puts(props.deviceName);
    VkPhysicalDeviceHostQueryResetFeatures host={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES};
    VkPhysicalDeviceFeatures2 f2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&host};
    vkGetPhysicalDeviceFeatures2(gpu,&f2);assert(host.hostQueryReset && f2.features.occlusionQueryPrecise && f2.features.pipelineStatisticsQuery);
    VkPhysicalDeviceFeatures features={.occlusionQueryPrecise=VK_TRUE,.pipelineStatisticsQuery=VK_TRUE};
    uint32_t nq=32;VkQueueFamilyProperties families[32];vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,families);
    unsigned family=0;while(family<nq && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) family++;
    assert(family<nq);float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&host,.pEnabledFeatures=&features,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    CHECK(vkCreateDevice(gpu,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
    VkCommandPool pool;CHECK(vkCreateCommandPool(device,&pi,NULL,&pool));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd;CHECK(vkAllocateCommandBuffers(device,&ca,&cmd));
    VkShaderModule shaders[2];
    const uint32_t *codes[]={query_vert_spv,query_frag_spv};size_t sizes[]={sizeof(query_vert_spv),sizeof(query_frag_spv)};
    for(unsigned i=0;i<2;i++) {
        VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizes[i],.pCode=codes[i]};
        CHECK(vkCreateShaderModule(device,&sm,NULL,&shaders[i]));
    }
    VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&pl,NULL,&layout));
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS};
    VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.subpassCount=1,.pSubpasses=&sub};
    VkRenderPass pass;CHECK(vkCreateRenderPass(device,&rp,NULL,&pass));
    VkFramebufferCreateInfo fb={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.width=8,.height=8,.layers=1};
    VkFramebuffer framebuffer;CHECK(vkCreateFramebuffer(device,&fb,NULL,&framebuffer));
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=shaders[0],.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=shaders[1],.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState dynamic[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dynamic};
    VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,
        .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,
        .pColorBlendState=&cb,.pDynamicState=&ds,.layout=layout,.renderPass=pass};
    PipelineBinding binding={0};CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gp,NULL,&binding.pipeline));
    VkQueryPool pools[3];
    for(unsigned i=0;i<3;i++) {
        VkQueryPoolCreateInfo qp={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=i==1?VK_QUERY_TYPE_PIPELINE_STATISTICS:VK_QUERY_TYPE_OCCLUSION,
            .queryCount=1024,.pipelineStatistics=i==1?VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT:0};
        CHECK(vkCreateQueryPool(device,&qp,NULL,&pools[i]));
    }
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fi,NULL,&fence));
    unsigned cases=0;
    for(unsigned host_reset=0;host_reset<2;host_reset++) for(unsigned cost=0;cost<2;cost++) for(unsigned mixed=0;mixed<2;mixed++) for(unsigned cycle=0;cycle<2;cycle++) {
        CHECK(vkResetCommandBuffer(cmd,0));VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&bi));
        for(unsigned i=1;i<3;i++) vkCmdResetQueryPool(cmd,pools[i],0,1024);
        PGRAPHVkState r={.device=device,.command_buffer=cmd,.render_pass=pass,.framebuffer=framebuffer,
            .query_pool=pools[0],.cost_stats_pool=pools[1],.cost_occl_pool=pools[2],.host_query_reset=host_reset,
            .in_command_buffer=true,.max_queries_in_flight=1024,.pipeline_binding=&binding,.pipeline_binding_changed=true,
            .cost_pending_slot=-1,.cost_pools_reset=true};
        NV2AState d={.pgraph={.vk_renderer_state=&r,.surface_scale_factor=1,.surface_binding_dim={8,8},.surface_shape={0,0,8,8}}};
        cost_model=cost;cost_slots=interruptions=0;xemu_cost_q_open=false;memset(counters,0,sizeof(counters));
        workload(&d,mixed);CHECK(vkEndCommandBuffer(cmd));
        if(host_reset) {
            uint64_t unavailable[1024][2];memset(unavailable,0xff,sizeof(unavailable));
            assert(vkGetQueryPoolResults(device,pools[0],0,r.num_queries_in_flight,sizeof(unavailable),unavailable,sizeof(unavailable[0]),
                VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)==VK_NOT_READY);
            for(int i=0;i<r.num_queries_in_flight;i++) assert(!unavailable[i][1]);
        }
        VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&si,fence));
        /* This is a standalone GPU test, not a wait added to the core. */
        CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));CHECK(vkResetFences(device,1,&fence));
        uint64_t results[1024];CHECK(vkGetQueryPoolResults(device,pools[0],0,r.num_queries_in_flight,sizeof(results),results,sizeof(results[0]),VK_QUERY_RESULT_64_BIT));
        check_results(results);cases++;
    }
    assert(!errors);printf("Vulkan query scope: %u result/reset/cost-model workloads, zero validation errors\n",cases);
    puts("237 query windows: render passes and pipeline binds 237 -> 1 (host reset)");
    vkDestroyFence(device,fence,NULL);for(unsigned i=0;i<3;i++) vkDestroyQueryPool(device,pools[i],NULL);
    vkDestroyPipeline(device,binding.pipeline,NULL);vkDestroyFramebuffer(device,framebuffer,NULL);vkDestroyRenderPass(device,pass,NULL);
    vkDestroyPipelineLayout(device,layout,NULL);for(unsigned i=0;i<2;i++) vkDestroyShaderModule(device,shaders[i],NULL);
    vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
}

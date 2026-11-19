/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <libretro_vulkan.h>
#include "hw/xbox/nv2a/pgraph/vk/libretro_shared.h"
#include "libretro/vk-shader-object.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
#define G_N_ELEMENTS(a) (sizeof(a)/sizeof((a)[0]))
#define g_new0(type,n) ((type*)calloc((n),sizeof(type)))
#define g_free free
#define LRLOG_INFO(...) ((void)0)
#define LRLOG_ERROR(...) ((void)0)
static bool use_vulkan;
static retro_environment_t environ_cb;
static bool opt_vk_dynamic=true,opt_vk_gpl;
static uint32_t xemu_vk_instance_api_version=VK_API_VERSION_1_3;
XemuLibretroVk xemu_libretro_vk;
/* OPTION_BLOCK */
static const char *option="disabled",*missing_proc;
static bool advertise=true,support_so=true,support_dynamic=true;
static uint32_t api=VK_API_VERSION_1_3;
static bool created_so,created_dynamic,created_so_extension,created_required;
static unsigned create_calls,query_so_calls,wrapper_calls;
static VkPhysicalDeviceFeatures created_features;
static bool frontend(unsigned command,void *data){
 assert(command==RETRO_ENVIRONMENT_GET_VARIABLE);
 struct retro_variable *v=data;assert(!strcmp(v->key,"xemu_vk_prepared_vertex"));v->value=option;return true;
}
static void VKAPI_CALL dummy(void){}
static void VKAPI_CALL queue(VkDevice d,uint32_t f,uint32_t i,VkQueue *q){assert(d && !f && i<2);*q=(VkQueue)(uintptr_t)(4+i);}
static PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice d,const char *name){
 assert(d);if(missing_proc&&!strcmp(name,missing_proc))return NULL;
 if(!strcmp(name,"vkGetDeviceQueue"))return(PFN_vkVoidFunction)queue;
 return dummy;
}
static VkResult VKAPI_CALL physicals(VkInstance i,uint32_t *n,VkPhysicalDevice *g){assert(i);if(g)*g=(VkPhysicalDevice)(uintptr_t)2;*n=1;return VK_SUCCESS;}
static void VKAPI_CALL features(VkPhysicalDevice g,VkPhysicalDeviceFeatures *f){
 assert(g);for(size_t n=0;n<sizeof(*f)/sizeof(VkBool32);n++)((VkBool32*)f)[n]=VK_TRUE;
}
static void VKAPI_CALL properties(VkPhysicalDevice g,VkPhysicalDeviceProperties *p){assert(g);memset(p,0,sizeof(*p));p->apiVersion=api;}
static void VKAPI_CALL queues(VkPhysicalDevice g,uint32_t *n,VkQueueFamilyProperties *p){
 assert(g);if(p){memset(p,0,sizeof(*p));p->queueFlags=VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT;p->queueCount=2;}*n=1;
}
static VkResult VKAPI_CALL present(VkPhysicalDevice g,uint32_t f,VkSurfaceKHR s,VkBool32 *p){(void)g;(void)f;(void)s;*p=VK_TRUE;return VK_SUCCESS;}
static VkResult VKAPI_CALL extensions(VkPhysicalDevice g,const char *layer,uint32_t *n,VkExtensionProperties *p){
 assert(g&&!layer);if(p){memset(p,0,(advertise?2:1)*sizeof(*p));strcpy(p[0].extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME);if(advertise)strcpy(p[1].extensionName,VK_EXT_SHADER_OBJECT_EXTENSION_NAME);}*n=advertise?2:1;return VK_SUCCESS;
}
static void VKAPI_CALL features2(VkPhysicalDevice g,VkPhysicalDeviceFeatures2 *f){
 assert(g);for(VkBaseOutStructure *s=f->pNext;s;s=s->pNext){
  if(s->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES){VkPhysicalDeviceVulkan13Features *p=(void*)s;p->dynamicRendering=support_dynamic;p->shaderDemoteToHelperInvocation=VK_TRUE;}
  if(s->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT){query_so_calls++;((VkPhysicalDeviceShaderObjectFeaturesEXT*)s)->shaderObject=support_so;}
 }
}
static VkResult VKAPI_CALL create(VkPhysicalDevice g,const VkDeviceCreateInfo *ci,const VkAllocationCallbacks *a,VkDevice *d){
 assert(g&&!a&&ci->pEnabledFeatures);create_calls++;created_features=*ci->pEnabledFeatures;
 created_so=created_dynamic=created_so_extension=created_required=false;
 for(unsigned n=0;n<ci->enabledExtensionCount;n++){
  if(!strcmp(ci->ppEnabledExtensionNames[n],VK_EXT_SHADER_OBJECT_EXTENSION_NAME))created_so_extension=true;
  if(!strcmp(ci->ppEnabledExtensionNames[n],VK_KHR_SWAPCHAIN_EXTENSION_NAME))created_required=true;
 }
 for(const VkBaseInStructure *s=ci->pNext;s;s=s->pNext){
  if(s->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES)created_dynamic=((const VkPhysicalDeviceVulkan13Features*)s)->dynamicRendering;
  if(s->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT)created_so=((const VkPhysicalDeviceShaderObjectFeaturesEXT*)s)->shaderObject;
 }
 *d=(VkDevice)(uintptr_t)3;return VK_SUCCESS;
}
static VkDevice wrapper(VkPhysicalDevice gpu,void *opaque,const VkDeviceCreateInfo *ci){
 assert(opaque==(void*)(uintptr_t)9);wrapper_calls++;VkDevice d;assert(create(gpu,ci,NULL,&d)==VK_SUCCESS);return d;
}
static PFN_vkVoidFunction VKAPI_CALL instance_proc(VkInstance instance,const char *name){
 assert(instance);
#define RESOLVE(n,f) if(!strcmp(name,#n))return(PFN_vkVoidFunction)f;
 RESOLVE(vkEnumeratePhysicalDevices,physicals)
 RESOLVE(vkGetPhysicalDeviceFeatures,features)
 RESOLVE(vkGetPhysicalDeviceQueueFamilyProperties,queues)
 RESOLVE(vkGetPhysicalDeviceSurfaceSupportKHR,present)
 RESOLVE(vkEnumerateDeviceExtensionProperties,extensions)
 RESOLVE(vkCreateDevice,create)
 RESOLVE(vkGetDeviceProcAddr,device_proc)
 RESOLVE(vkGetPhysicalDeviceProperties,properties)
 RESOLVE(vkGetPhysicalDeviceFeatures2,features2)
#undef RESOLVE
 return NULL;
}
/* HAS_EXT */
/* DEVICE_FN */
static void reset(void){
#if !defined(__ANDROID__) && !defined(XEMU_DISABLE_READY_VK)
 memset(&vk_prepared_vertex,0,sizeof(vk_prepared_vertex));
#endif
 use_vulkan=false;option="disabled";missing_proc=NULL;advertise=support_so=support_dynamic=true;api=VK_API_VERSION_1_3;
 query_so_calls=wrapper_calls=create_calls=0;
}
static void run(bool wrapped,const VkPhysicalDeviceFeatures *required){
 struct retro_vulkan_context context={0};const char *ext=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
 assert(xemu_vk_make_device(&context,(VkInstance)(uintptr_t)1,VK_NULL_HANDLE,VK_NULL_HANDLE,instance_proc,
                           &ext,1,NULL,0,required,wrapped?wrapper:NULL,(void*)(uintptr_t)9));
 assert(create_calls && created_required && context.device && xemu_libretro_vk.active);
 assert(created_features.depthClamp && created_features.geometryShader && created_features.fillModeNonSolid);
 assert(created_features.alphaToOne==(required?required->alphaToOne:VK_FALSE));
 assert(created_features.depthBounds==(required?required->depthBounds:VK_FALSE));
 assert(!wrapped || wrapper_calls==1);
}
int main(void){
 environ_cb=frontend;
 latch_vk_prepared_vertex_option();
 reset();run(false,NULL);assert(!created_so && !created_dynamic && !created_so_extension && !query_so_calls);
 assert(!xemu_libretro_vk.shader_object&&!xemu_libretro_vk.dynamic_rendering);
 reset();option="enabled";run(false,NULL);
#if !defined(__ANDROID__) && !defined(XEMU_DISABLE_READY_VK)
 assert(created_so&&created_dynamic&&created_so_extension&&query_so_calls==1);
 assert(xemu_libretro_vk.shader_object&&xemu_libretro_vk.dynamic_rendering);
 /* Pending changes on a context reset cannot mutate the first-device latch. */
 option="disabled";run(true,NULL);assert(created_so&&created_dynamic&&xemu_libretro_vk.shader_object);
 reset();use_vulkan=false;option="enabled";update_vk_prepared_vertex_option();latch_vk_prepared_vertex_option();assert(!vk_prepared_vertex.latched);run(true,NULL);assert(created_so&&created_dynamic);
 reset();option="enabled";support_dynamic=false;run(false,NULL);assert(!created_so&&!created_dynamic&&!xemu_libretro_vk.shader_object);
 reset();option="enabled";support_so=false;run(false,NULL);assert(!created_so&&!created_dynamic&&!xemu_libretro_vk.shader_object);
 reset();option="enabled";advertise=false;run(false,NULL);assert(!created_so&&!created_so_extension&&!xemu_libretro_vk.shader_object);
 reset();option="enabled";api=VK_API_VERSION_1_2;run(false,NULL);assert(!created_so&&!created_dynamic&&!created_so_extension&&!query_so_calls);
 reset();option="enabled";missing_proc="vkCreateShadersEXT";run(false,NULL);assert(created_so&&created_dynamic&&!xemu_libretro_vk.shader_object);
 reset();option="enabled";missing_proc="vkCmdSetDepthClampEnableEXT";run(false,NULL);assert(!xemu_libretro_vk.shader_object);
 reset();option="enabled";missing_proc="vkCmdSetAlphaToOneEnableEXT";run(false,NULL);assert(xemu_libretro_vk.shader_object); /* not enabled, not required */
 reset();option="enabled";missing_proc="vkCmdSetAlphaToOneEnableEXT";VkPhysicalDeviceFeatures required={.alphaToOne=VK_TRUE,.depthBounds=VK_TRUE,.tessellationShader=VK_TRUE};run(true,&required);assert(!xemu_libretro_vk.shader_object);assert(created_features.tessellationShader&&created_features.alphaToOne&&created_features.depthBounds);
 reset();run(true,&required);assert(!created_so&&!created_dynamic&&!created_so_extension);assert(created_features.tessellationShader&&created_features.alphaToOne);
#else
 assert(!created_so&&!created_dynamic&&!created_so_extension&&!query_so_calls);
#endif
 puts("Shader-object negotiation: actual feature enable chain, default off, wrapper requirements, first Vulkan latch/reset, optional misses and old devices PASS");
 return 0;
}

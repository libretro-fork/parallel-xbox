/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "hw/xbox/nv2a/pgraph/vk/ready-config.h"
#define HAVE_NO_LANGEXTRA 1
#include "ui/libretro_core_options.h"
static bool use_vulkan;
static retro_environment_t environ_cb;
/* UI */
static const char *value;
static bool frontend(unsigned command,void *arg){assert(command==RETRO_ENVIRONMENT_GET_VARIABLE);struct retro_variable *v=arg;assert(!strcmp(v->key,"xemu_vk_prepared_vertex"));v->value=value;return true;}
int main(void){
 unsigned found=0;for(unsigned i=0;option_defs_us[i].key;i++)if(!strcmp(option_defs_us[i].key,"xemu_vk_prepared_vertex")){found++;assert(!strcmp(option_defs_us[i].default_value,"disabled"));assert(!strcmp(option_defs_us[i].desc,"Prepared Shaders (Vulkan, Restart)"));assert(strstr(option_defs_us[i].info,"geometry")&&strstr(option_defs_us[i].info,"fragment")&&strstr(option_defs_us[i].info,"GPU")&&strstr(option_defs_us[i].info,"startup"));}
#if !defined(__ANDROID__) && !defined(XEMU_DISABLE_READY_VK)
 assert(found==1);environ_cb=frontend;value="enabled";update_vk_prepared_vertex_option();use_vulkan=false;latch_vk_prepared_vertex_option();assert(!pgraph_vk_prepared_vertex_enabled()&&!vk_prepared_vertex.latched);use_vulkan=true;latch_vk_prepared_vertex_option();assert(pgraph_vk_prepared_vertex_enabled()&&vk_prepared_vertex.latched);value="disabled";update_vk_prepared_vertex_option();latch_vk_prepared_vertex_option();assert(pgraph_vk_prepared_vertex_enabled());use_vulkan=false;latch_vk_prepared_vertex_option();assert(pgraph_vk_prepared_vertex_enabled());memset(&vk_prepared_vertex,0,sizeof(vk_prepared_vertex));use_vulkan=true;value=NULL;update_vk_prepared_vertex_option();latch_vk_prepared_vertex_option();assert(!pgraph_vk_prepared_vertex_enabled());
 memset(&vk_prepared_vertex,0,sizeof(vk_prepared_vertex));use_vulkan=false;value="enabled";update_vk_prepared_vertex_option();latch_vk_prepared_vertex_device_option();assert(pgraph_vk_prepared_vertex_enabled());value="disabled";update_vk_prepared_vertex_option();latch_vk_prepared_vertex_device_option();assert(pgraph_vk_prepared_vertex_enabled());
#else
 (void)frontend;(void)use_vulkan;(void)environ_cb;assert(!found&&!pgraph_vk_prepared_vertex_enabled());update_vk_prepared_vertex_option();latch_vk_prepared_vertex_option();latch_vk_prepared_vertex_device_option();
#endif
 puts("Vulkan prepared option: default-off, independent GL/Vulkan first-content latch and restart-only updates PASS");return 0;
}

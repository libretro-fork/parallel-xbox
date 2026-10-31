# SPDX-License-Identifier: GPL-2.0-or-later
"""Check fixed shader creation borrows words until native creation completes."""
from pathlib import Path
import os, subprocess, sys, tempfile
from importlib.util import spec_from_file_location, module_from_spec
r=Path(sys.argv[1]).resolve()
spec=spec_from_file_location("depth_test",r/"libretro/tests/test-spv-depth.py")
m=module_from_spec(spec);spec.loader.exec_module(m)
s=(r/"hw/xbox/nv2a/pgraph/vk/glsl.c").read_text()
production="\n".join(m.function(s,n) for n in [
 "pgraph_vk_create_shader_module_from_words",
 "pgraph_vk_create_shader_module_from_spv",
 "pgraph_vk_create_fixed_shader_module_from_glsl"])
prefix=r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
typedef struct { VkDevice device; } PGRAPHVkState;
typedef struct { unsigned char *data; unsigned len; } GByteArray;
static uint32_t words[]={0x07230203,0x00010000,0,1,0};
static GByteArray array={(unsigned char*)words,sizeof(words)};
static unsigned live,created,released;
static VkShaderStageFlagBits expected_stage;
static const char *expected_source;
static GByteArray *pgraph_vk_compile_glsl_to_spv(VkShaderStageFlagBits stage,const char *source){
 assert(!live && stage==expected_stage && source==expected_source);live=1;return &array;
}
static void g_byte_array_unref(GByteArray *a){
 assert(a==&array && live==2);live=0;released++;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device,const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *allocator,VkShaderModule *out){
 assert(device==(VkDevice)(uintptr_t)7 && !allocator && live==1);
 assert(info->sType==VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
 assert(info->codeSize==sizeof(words) && info->pCode==words);
 assert(!info->pNext && !info->flags);live=2;created++;
 *out=(VkShaderModule)(uintptr_t)created;return VK_SUCCESS;
}
"""
suffix=r"""
int main(void){
 PGRAPHVkState r={(VkDevice)(uintptr_t)7};
 VkShaderStageFlagBits stages[]={VK_SHADER_STAGE_VERTEX_BIT,VK_SHADER_STAGE_GEOMETRY_BIT,VK_SHADER_STAGE_FRAGMENT_BIT,VK_SHADER_STAGE_COMPUTE_BIT};
 uint32_t snapshot[5];memcpy(snapshot,words,sizeof(words));
 for(unsigned i=0;i<10000;i++){
  expected_stage=stages[i%4];expected_source="fixed program";
  VkShaderModule module=pgraph_vk_create_fixed_shader_module_from_glsl(&r,expected_stage,expected_source);
  assert((uintptr_t)module==i+1 && !live && created==i+1 && released==i+1);
  assert(!memcmp(snapshot,words,sizeof(words)));
 }
 return 0;
}
"""
cc=sys.argv[2] if len(sys.argv)>2 else "gcc"
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='fixed-shader-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(prefix+production+suffix)
 exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r/'deps/vulkan-headers'),str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
print('Fixed shader ownership: 10000 borrowed creations, exact pointer/length and release ordering passed')

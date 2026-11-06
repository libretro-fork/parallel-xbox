# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the startup policy and prove the disabled recording code is unchanged."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else "gcc"
path = "hw/xbox/nv2a/pgraph/vk/texture.c"
source = (repo / path).read_text(encoding="utf-8")
baseline = None
if len(sys.argv) > 3:
    baseline = subprocess.check_output(["git", "show", sys.argv[3] + ":" + path], cwd=repo).decode()
a = source.index("    /* Select the existing scaled-depth fallback once.")
b = source.index("    texture_cache_init(r);", a)
policy = source[a:b]
if baseline is not None:
    old = baseline.replace("\r\n", "\n")
    restored = (source[:a] + source[b:]).replace("format_native_blit_enabled", "format_blit_supported")
    assert restored == old, "recording paths changed beyond the startup policy"

def helper(code, name):
    a = code.index("static bool " + name + "(")
    b = code.index("\n/* Native-size transfer", a)
    return code[a:b].replace(name, "format_native_blit_enabled")

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan_core.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct { VkPhysicalDevice physical_device; struct {VkFormat format;bool ok;} blit_formats[8]; int blit_formats_n; } PGRAPHVkState;
static bool enabled;
static unsigned queries;
static bool nv2a_get_high_resolution_depth_textures(void) { return enabled; }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice d,VkFormat f,VkFormatProperties *p) {
    (void)d;(void)f;queries++;
    memset(p,0,sizeof(*p));p->optimalTilingFeatures=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT;
}
'''
test = r'''
static void init(PGRAPHVkState *r) { POLICY }
int main(void) {
 const VkFormat depth[]={VK_FORMAT_D16_UNORM,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT_S8_UINT};
 const VkFormat color[]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R5G6B5_UNORM_PACK16};
 for(unsigned mode=0;mode<2;mode++) {
  PGRAPHVkState r={0};enabled=mode;queries=0;init(&r);
  assert(queries==0&&r.blit_formats_n==(mode?3:0));
  for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<3;i++) {
   assert(format_native_blit_enabled(&r,depth[i])==!mode);
   assert(format_native_blit_enabled(&r,color[i]));
  }
  assert(queries==(mode?3:6));
 }
 puts("PASS: startup depth policy, unchanged color capabilities, cached repeat queries");
}
'''.replace("POLICY", policy)
with tempfile.TemporaryDirectory(prefix="highres-depth-") as directory:
    folder = Path(directory)
    src = folder / "test.c"
    src.write_text(prefix + helper(source, "format_native_blit_enabled") + test)
    exe = folder / ("test.exe" if os.name == "nt" else "test")
    flags = ["-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare", "-I" + str(repo / "deps/vulkan-headers")]
    subprocess.run([cc, *flags, str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    # Identical function text also preserves compiler treatment, not just results.
    if baseline is not None:
        assert helper(source, "format_native_blit_enabled") == helper(old, "format_blit_supported")
        print("PASS: disabled recording paths and native-transfer lookup unchanged from supplied baseline")

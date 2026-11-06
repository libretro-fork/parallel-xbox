# SPDX-License-Identifier: GPL-2.0-or-later
"""Qualify D16 borrowing exclusions and optional real-device sampling/lifetime."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("checkout", type=Path)
p.add_argument("cc", nargs="?", default="gcc")
p.add_argument("--gpu", action="store_true")
p.add_argument("--benchmark", action="store_true", help="time GPU write/copy-or-borrow/sample batches instead of the sampling grid")
p.add_argument("--size", type=int, default=256, choices=(32, 128, 256, 512, 1024), help="benchmark image dimension; sampling grid stays 256")
a = p.parse_args()
if (a.benchmark and not a.gpu) or (a.size != 256 and not a.benchmark):
    p.error("--benchmark requires --gpu, and --size requires --benchmark")
r = a.checkout.resolve()
vk = r / "hw/xbox/nv2a/pgraph/vk"

def function(path, name):
    source = path.read_text(encoding="utf-8")
    match = re.search(r"^(?:static )?(?:inline )?[\w *]+\b" + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]

eligibility = function(vk / "texture.c", "d16_surface_can_borrow")
assert "nv2a_get_" not in eligibility
cpu = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <vulkan/vulkan_core.h>
#define ctz32 __builtin_ctz
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef struct {bool color,native_resolution,draw_dirty;unsigned width,height;struct{VkFormat vk_format;}host_fmt;struct{unsigned anti_aliasing;}shape;} SurfaceBinding;
typedef struct {unsigned color_format,dimensionality,levels;bool cubemap,border;} TextureShape;
typedef struct {bool d16_sampling;SurfaceBinding *zeta_binding,*color_binding;} PGRAPHVkState;
/* ELIGIBILITY */
int main(void){
 unsigned count=0;SurfaceBinding s={.draw_dirty=true,.width=16,.height=16,.host_fmt={VK_FORMAT_D16_UNORM},.shape={NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1}};
 PGRAPHVkState r={.d16_sampling=true};TextureShape t={.color_format=NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED,.dimensionality=2,.levels=1};
 for(unsigned fmt=0;fmt<2;fmt++)for(unsigned min=0;min<16;min++)for(unsigned mag=0;mag<8;mag++)for(unsigned native=0;native<2;native++)for(unsigned cap=0;cap<2;cap++)for(unsigned aniso=1;aniso<3;aniso++)for(unsigned bound=0;bound<3;bound++)for(unsigned aa=0;aa<2;aa++)for(unsigned levels=0;levels<3;levels++){
  t.color_format=fmt?NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED:NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FIXED;t.levels=levels;r.d16_sampling=cap;r.color_binding=bound==1?&s:NULL;r.zeta_binding=bound==2?&s:NULL;s.shape.anti_aliasing=aa?NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_CORNER_2:NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1;
  uint32_t filter=(min<<__builtin_ctz(NV_PGRAPH_TEXFILTER0_MIN))|(mag<<__builtin_ctz(NV_PGRAPH_TEXFILTER0_MAG));
  bool expected=min>=1&&min<=6&&(mag==1||mag==2)&&!native&&cap&&aniso==1&&!bound&&!aa&&levels==1;
  assert(d16_surface_can_borrow(&r,&s,&t,native,filter,aniso)==expected);count++;
 }
 r=(PGRAPHVkState){.d16_sampling=true};s.shape.anti_aliasing=NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1;t.levels=1;
 uint32_t filter=(1u<<__builtin_ctz(NV_PGRAPH_TEXFILTER0_MIN))|(1u<<__builtin_ctz(NV_PGRAPH_TEXFILTER0_MAG));
 assert(d16_surface_can_borrow(&r,&s,&t,false,filter,1));
 for(unsigned guard=0;guard<12;guard++){
  SurfaceBinding x=s;TextureShape y=t;
  switch(guard){case 0:x.color=true;break;case 1:x.native_resolution=true;break;case 2:x.width=0;break;case 3:x.height=0;break;case 4:x.host_fmt.vk_format=VK_FORMAT_D24_UNORM_S8_UINT;break;case 5:y.dimensionality=1;break;case 6:y.dimensionality=3;break;case 7:y.cubemap=true;break;case 8:y.border=true;break;case 9:y.color_format=NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT;break;case 10:y.color_format=NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8;break;case 11:x.draw_dirty=false;break;}
  assert(!d16_surface_can_borrow(&r,&x,&y,false,filter,1));count++;
 }
 printf("PASS: %u production D16 selection and exclusion cases\n",count);return 0;
}
'''.replace("/* ELIGIBILITY */", eligibility)
env = dict(os.environ)
if os.name == "nt":
    env["PATH"] = str(Path(a.cc).resolve().parent) + os.pathsep + env.get("PATH", "")
with tempfile.TemporaryDirectory(prefix="d16-borrow-") as directory:
    folder = Path(directory)
    test = folder / "cpu.c"
    test.write_text(cpu)
    exe = folder / ("cpu.exe" if os.name == "nt" else "cpu")
    command = [a.cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I" + str(r), "-I" + str(r / "deps/vulkan-headers"), str(test), "-o", str(exe)]
    subprocess.run(command, check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env)
    # A valid render-attachment feedback rejection is essential, not optional.
    broken = cpu.replace("surface == r->zeta_binding ||", "")
    assert broken != cpu
    test.write_text(broken)
    subprocess.run(command, check=True, env=env)
    result = subprocess.run([str(exe)], capture_output=True, env=env)
    assert result.returncode != 0
    print("PASS: missing active-depth feedback guard fails the independent oracle")
    if not a.gpu:
        raise SystemExit(0)

    helper_source = (r / "libretro/tests/spv-clear-vulkan.h").read_text()
    helpers = helper_source.split("static VkPipeline pipeline(")[0]
    pipeline = function(r / "libretro/tests/spv-clear-vulkan.h", "pipeline")
    helpers += pipeline
    depth_pipeline = pipeline.replace("pipeline(", "depth_pipeline(").replace(".attachmentCount=gs?0:1", ".attachmentCount=0").replace("VkGraphicsPipelineCreateInfo gp=", "VkPipelineDepthStencilStateCreateInfo depth={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_ALWAYS};VkGraphicsPipelineCreateInfo gp=").replace(".pColorBlendState=&cb", ".pDepthStencilState=&depth,.pColorBlendState=&cb")
    helpers += "\n" + depth_pipeline
    helpers = helpers.replace("VkInstanceCreateInfo ci={", "VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;VkValidationFeaturesEXT validation={.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,.enabledValidationFeatureCount=1,.pEnabledValidationFeatures=&sync};VkInstanceCreateInfo ci={.pNext=validation_enabled?&validation:NULL,")
    production = function(vk / "renderer.h", "pgraph_vk_surface_base_layout")
    production += "\n" + "\n".join(function(vk / "image.c", n) for n in ("check_format_has_depth_component", "check_format_has_stencil_component", "pgraph_vk_transition_image_layout"))
    production += "\n" + "\n".join(function(vk / "texture.c", n) for n in ("create_texture_view", "s2t_borrow_sync", "pgraph_vk_surface_left_shader_read"))
    production += "\n" + function(vk / "surface.c", "pgraph_vk_surface_ensure_attachment_layout")
    source = (vk / "texture.c").read_text(encoding="utf-8")
    start = source.index("    VkImageLayout source_layout =", source.index("static void copy_zeta_surface_to_texture("))
    copy_begin = source[start:source.index("    VkImage copy_src_image", start)]
    start = source.index("    pgraph_vk_transition_image_layout(", source.index("    vkCmdCopyImageToBuffer(", start))
    copy_end = source[start:source.index("    VkBuffer texture_source_buffer;", start)]
    gpu = Path(__file__).with_suffix(".c").read_text().replace("/* GPU HELPERS */", helpers).replace("/* PRODUCTION */", production).replace("/* COPY SOURCE BEGIN */", copy_begin).replace("/* COPY SOURCE END */", copy_end)
    if a.size != 256:
        gpu = re.sub(r"\b256(u|\.0)?\b", lambda m: str(a.size) + (m[1] or ""), gpu)
        gpu = re.sub(r"\b65536\b", str(a.size * a.size), gpu)
    test = folder / "gpu.c"
    test.write_text(gpu)
    exe = folder / ("gpu.exe" if os.name == "nt" else "gpu")
    command = [a.cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-maybe-uninitialized", "-Wno-misleading-indentation", "-Wno-missing-field-initializers", "-Wno-sign-compare", "-I" + str(r), "-I" + str(r / "deps/volk"), "-I" + str(r / "deps/vulkan-headers"), "-I" + str(r / "deps/spirv-headers/include"), str(test), str(r / "hw/xbox/nv2a/pgraph/spv/glslc.c"), str(r / "hw/xbox/nv2a/pgraph/spv/glslc_lex.c"), str(r / "deps/volk/volk.c"), "-o", str(exe)]
    if os.name != "nt":
        command += ["-ldl", "-lm"]
    subprocess.run(command, check=True, env=env)
    subprocess.run([str(exe)] + (["benchmark"] if a.benchmark else []), check=True, env=env, timeout=120)
    if a.benchmark:
        raise SystemExit(0)
    negative = subprocess.run([str(exe), "negative-view"], capture_output=True, text=True, env=env, timeout=120)
    assert negative.returncode == 1 and "mismatching batches" in negative.stdout
    print("PASS: wrong depth-view component mapping fails the sampling oracle")

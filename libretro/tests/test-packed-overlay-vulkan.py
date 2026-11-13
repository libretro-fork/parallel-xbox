# SPDX-License-Identifier: GPL-2.0-or-later
"""Production Vulkan overlay converter + immutable words, real Vulkan sync validation."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();s=(r/'hw/xbox/nv2a/pgraph/vk/pvideo.c').read_text().replace('#include "renderer.h"','').replace('#include "libretro/dsp-ownership.h"','')
reference=(r/'libretro/tests/test-packed-overlay.c').read_text();a2=reference.index('static uint8_t clip');b2=reference.index('static unsigned cases',a2)
module_source=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
helpers=[]
for name in ('pgraph_vk_create_shader_module_from_words','pgraph_vk_create_utility_shader_module'):
 start=module_source.index('VkShaderModule '+name+'(');end=module_source.index('{',start)+1;depth=1
 while depth:
  depth+=(module_source[end]=='{')-(module_source[end]=='}');end+=1
 helpers.append(module_source[start:end])
code=Path(__file__).with_suffix('.c').read_text().replace('/* MODULE HELPERS */','\n'.join(helpers)).replace('/* PRODUCTION */',s).replace('/* REFERENCE */',reference[a2:b2])
with tempfile.TemporaryDirectory(prefix='vk-packed-overlay-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 binary=os.environ.get('VULKAN_OVERLAY_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('VULKAN_OVERLAY_CFLAGS','-fsanitize=undefined -fno-sanitize-recover=all'))
 libs=shlex.split(os.environ.get('VULKAN_OVERLAY_LIBS','-lvulkan -lm'))
 command=[sys.argv[2] if len(sys.argv)>2 else 'gcc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-function','-Wno-missing-field-initializers','-Wno-misleading-indentation']+flags+['-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/utility.c')]+libs+['-o',binary]
 subprocess.run(command,check=True)
 if not os.environ.get('VULKAN_OVERLAY_SKIP_RUN'):subprocess.run([binary],check=True,timeout=60)

# SPDX-License-Identifier: GPL-2.0-or-later
"""Production Vulkan overlay converter + internal compiler, real Vulkan sync validation."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();s=(r/'hw/xbox/nv2a/pgraph/vk/pvideo.c').read_text().replace('#include "renderer.h"','')
reference=(r/'libretro/tests/test-packed-overlay.c').read_text();a2=reference.index('static uint8_t clip');b2=reference.index('static unsigned cases',a2)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s).replace('/* REFERENCE */',reference[a2:b2])
with tempfile.TemporaryDirectory(prefix='vk-packed-overlay-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 command=[sys.argv[2] if len(sys.argv)>2 else 'gcc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-function','-Wno-missing-field-initializers','-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(r),'-I'+str(r/'deps/SPIRV-Reflect/include'),str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),'-lvulkan','-lm','-o',str(p/'test')]
 subprocess.run(command,check=True);subprocess.run([str(p/'test')],check=True,timeout=60)

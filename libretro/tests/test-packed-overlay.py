# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual packed-row code and production GLSL vs old native-filtered RGBA."""
from pathlib import Path
import json,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();s=(r/'hw/xbox/nv2a/pgraph/vk/pvideo.c').read_text()
a=s.index('static void pvideo_pack_rows(');b=s.index('\n}\n',a)+2;pack=s[a:b]
# The reference literal moved out of the runtime's immutable-SPIR-V path.
# Match its complete initializer, excluding other utility shader literals.
source=(r/'hw/xbox/nv2a/pgraph/spv/utility-glsl.h').read_text()
literal=re.search(r'static\s+const\s+char\s*\*\s*pvideo_convert_glsl\s*=\s*((?:\s*"(?:[^"\\]|\\.)*")+)\s*;',source)
assert literal is not None, 'missing packed-overlay reference shader initializer'
glsl=''.join(json.loads(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',literal.group(1)))
glsl=glsl.replace('layout(push_constant, std430)', 'layout(std140, binding=2)')
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',pack+'\nstatic const char *pvideo_convert_glsl='+json.dumps(glsl)+';')
with tempfile.TemporaryDirectory(prefix='packed-overlay-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-lEGL','-lGL','-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],env=dict(os.environ,EGL_PLATFORM='surfaceless',ASAN_OPTIONS='detect_leaks=0'),check=True,timeout=60)

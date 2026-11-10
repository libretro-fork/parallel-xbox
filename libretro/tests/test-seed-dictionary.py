# SPDX-License-Identifier: GPL-2.0-or-later
"""Linux CI: validate the actual bundled dictionary with native stage selectors.
The source file is mapped; each zlib stream decodes into one final owned record
buffer. No decoded file, second payload buffer or whole-file read is needed.
"""
from pathlib import Path
import os,subprocess,sys,tempfile,struct,zlib
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(source,name):
 a=source.index(name);b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
native=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text(encoding='utf-8')
selectors='\n'.join(function(native,'static bool '+name+'(') for name in ['vk_module_line_options','vk_module_triangle_options','vk_module_quad_options'])
selectors+='\n'+function((r/'hw/xbox/nv2a/pgraph/glsl/geom.c').read_text(encoding='utf-8'),'bool pgraph_glsl_need_geom(')
code=Path(__file__).with_suffix('.c').read_text().replace('/* SELECTORS */',selectors)
with tempfile.TemporaryDirectory(prefix='seed-dictionary-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <stdlib.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 (d/'test.c').write_text(code);exe=d/'test'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-I'+str(d),'-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),str(d/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/vsh.c'),str(r/'hw/xbox/nv2a/pgraph/spv/psh.c'),'-Wl,--gc-sections','-lz','-o',str(exe)],check=True)
 source=r/'data/shader_seeds.bin'
 subprocess.run([str(exe),str(source)],check=True,timeout=60)
 bad=subprocess.run([str(exe),str(source),'--omit-migration'],capture_output=True,timeout=60)
 assert bad.returncode and b'fragment eligibility' in bad.stderr
 print('Required dictionary control rejected: omitted legacy migration')
 def fixture(raw,declared=None):
  compressed=zlib.compress(raw)
  return struct.pack('<III',0x44445358,1,1)+b'fixture'.ljust(32,b'\0')+struct.pack('<II',len(raw) if declared is None else declared,len(compressed))+compressed
 for label,data,reason in [
  ('truncated entries',struct.pack('<III',0x44445358,1,1),b'dictionary entries truncated'),
  ('stale Vulkan ABI',fixture(struct.pack('<IIII',0x44455358,1,1,0)+struct.pack('<IIII',3,1,1,0)+b'\0'),b'Vulkan state ABI mismatch'),
  ('missing Vulkan fixtures',fixture(struct.pack('<IIII',0x44455358,1,0,0)),b'fixture coverage decreased or absent'),
  ('short decoded owner',fixture(struct.pack('<IIII',0x44455358,1,0,0),4),b'decompression size mismatch'),
 ]:
  file=d/'bad.bin';file.write_bytes(data) # Tiny parser metadata witnesses only.
  result=subprocess.run([str(exe),str(file)],capture_output=True,timeout=30)
  assert result.returncode and reason in result.stderr,(label,result.stderr)
  print('Required dictionary control rejected:',label)

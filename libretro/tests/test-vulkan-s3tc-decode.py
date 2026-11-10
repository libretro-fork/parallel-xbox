# SPDX-License-Identifier: GPL-2.0-or-later
"""Production S3TC image decode, internal GLSL compiler, Vulkan synchronization validation."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve()
s=(r/'hw/xbox/nv2a/pgraph/vk/s3tc-decode.h').read_text()
import re
texture=(r/'hw/xbox/nv2a/pgraph/vk/texture.c').read_text()
stage=texture[texture.index('static void stage_texture('):texture.index('static void stage_texture(')+11000]
stage=stage[:stage.index('\nstatic ')]
assert 'TextureShape s = key->state;' in stage
assert 'key->texture_vram_offset' in stage and 'key->palette_vram_offset' in stage
assert not re.search(r'pgraph_get_texture_(?:shape|phys_addr|palette_phys_addr_length)\(',stage)
assert 'stage_texture(pg, cmd, texture_idx, &binding->key, &layout, skip_layers);' in texture
parts=[re.search(r'typedef struct TextureLevel \{.*?\} TextureLevel;',texture,re.S)[0]]
for name in ('stage_level','decode_level_3d'):
 pos=texture.index(name+'(');start=texture.rfind('\nstatic ',0,pos)+1;end=texture.index('{',pos)+1;depth=1
 while depth:
  depth+=(texture[end]=='{')-(texture[end]=='}');end+=1
 parts.append(texture[start:end])
code=Path(__file__).with_suffix('.c').read_text().replace('/* CPU PRODUCTION */','\n'.join(parts)).replace('/* PRODUCTION */',s)
with tempfile.TemporaryDirectory(prefix='vk-s3tc-') as tmp:
 p=Path(tmp); (p/'test.c').write_text(code)
 cmd=[sys.argv[2] if len(sys.argv)>2 else 'gcc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-function','-Wno-missing-field-initializers','-Wno-misleading-indentation','-I'+str(r),'-I'+str(r/'libretro/tests'),'-I'+str(r/'deps/spirv-headers/include'),str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/s3tc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/utility.c'),'-lm','-o',str(p/('test.exe' if os.name=='nt' else 'test'))]
 if os.name=='nt': cmd += ['-I'+str(r/'deps/vulkan-headers'),'-LC:/Windows/System32','-l:vulkan-1.dll','-static']
 else: cmd += ['-lvulkan',*shlex.split(os.environ.get('VK_S3TC_CFLAGS','-g' if os.environ.get('VK_S3TC_BENCH') else '-fsanitize=undefined -fno-sanitize-recover=all'))]
 env=os.environ.copy();env['PATH']=str(Path(cmd[0]).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run(cmd,check=True,env=env)
 exe=p/('test.exe' if os.name=='nt' else 'test')
 if os.environ.get('VK_S3TC_BINARY'):
  import shutil
  shutil.copy2(exe,os.environ['VK_S3TC_BINARY'])
 if not os.environ.get('VK_S3TC_SKIP_RUN'): subprocess.run([str(exe)],check=True,timeout=120,env=env)

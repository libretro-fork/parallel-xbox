# SPDX-License-Identifier: GPL-2.0-or-later
"""Production cube provenance and per-face copy routing with payload ownership."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2]
s=(r/'hw/xbox/nv2a/pgraph/vk/texture.c').read_text();parts=[]
for name in ('hash_mixed_cube_ram','preserve_cube_upload_sources','copy_cube_faces_to_texture'):
 m=re.search(r'^static (?:void|uint64_t) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[a:b])
assert 'hash_mixed_cube_ram(texture_data, texture_length / 6, cube_mask)' in s
assert 'preserve_cube_upload_sources(binding, skip_layers);' in s
assert 'preserve_cube_upload_sources(binding, gpu_mask);' in s
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='mixed-cube-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/'test.exe'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),str(d/'test.c'),str(r/'util/fast-hash.c'),str(r/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)

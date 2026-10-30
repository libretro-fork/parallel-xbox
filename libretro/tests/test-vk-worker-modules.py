# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production worker memo and array refcounts with bounded oracles.
Usage: python test-vk-worker-modules.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text();g=(repo/'libretro/glib/gcontainers.c').read_text()
def function(source,name):
 m=re.search(r'^(?:static )?(?:void|GArray \*|GByteArray \*)\s*'+name+r'\([^;]*?\)\s*\{',source,re.M);assert m,name
 a=source.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[m.start():b]
record=re.search(r'typedef struct VkSpecDone \{.*?\} VkSpecDone;',s,re.S)[0]
state=s[s.index('#define VK_SPEC_MODULE_SLOTS'):s.index('static void vk_spec_module_evict')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* RECORD */',record)
code=code.replace('/* PRODUCTION */','\n'.join([function(g,'g_array_ref'),function(g,'g_array_unref'),function(g,'g_byte_array_new_take'),state]+[function(s,n) for n in ['vk_spec_module_evict','vk_spec_modules_clear','vk_spec_compile_module','vk_spec_compile_state','vk_spec_ready_push','vk_spec_take_ready']]))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-worker-modules-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-misleading-indentation','-I'+str(repo/'libretro-common/include'),'-pthread','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)

# SPDX-License-Identifier: LGPL-2.1-or-later
"""Production overlay allocation acquisition and budget-aware Vulkan image pool."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(path,name):
 s=(r/path).read_text();m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\(',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
base='hw/xbox/nv2a/pgraph/vk/'
parts=[function(base+'texture.c',n) for n in ['image_ci_equal','image_pool_drop','image_pool_heap_over_budget','pgraph_vk_image_pool_put_with_view','pgraph_vk_image_pool_get_with_view','pgraph_vk_image_pool_put','pgraph_vk_image_pool_get','pgraph_vk_image_pool_flush']]
parts.append(function(base+'display.c','acquire_pvideo_image'))
src=(r/(base+'texture.c')).read_text();limit=re.search(r'^#define IMAGE_POOL_MAX_BYTES .+$',src,re.M)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* LIMIT */',limit).replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
with tempfile.TemporaryDirectory(prefix='pvideo-pool-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=30)

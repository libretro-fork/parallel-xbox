# SPDX-License-Identifier: GPL-2.0-or-later
"""Pixel-compare production crosshair attachment clears on a native Vulkan GPU."""
from pathlib import Path
import ctypes, os, re, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
def function(s,name):
 m=re.search(r'^static [^\n]*?\b'+name+r'\(',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
s=(r/'hw/xbox/nv2a/pgraph/vk/display.c').read_text()
a=s.index('static const VkClearColorValue crosshair_colors');b=s.index('/*\n * The display is drawn',a)
production=s[a:b]+'\n'+function(s,'frame_image_barrier')
h=(r/'libretro/tests/spv-clear-vulkan.h').read_text();a=h.index('#define CHECK');b=h.index('static VKAPI_ATTR',a)
helpers=h[a:b]+'\n'+'\n'.join(function(h,n) for n in ['gpu_message','memory_type','make_buffer','drop_buffer','begin','submit','host_barrier','gpu_init'])
q=(r/'libretro/tests/test-spv-geom.c').read_text();a=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=q.index('int line_tests(',a)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',production).replace('/* GPU HELPERS */',helpers).replace('/* QUIET ERRORS */',q[a:b])
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-crosshair-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-variable','-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/volk'),str(d/'test.c'),str(r/'deps/volk/volk.c'),'-o',str(exe)]
 if os.name!='nt':cmd+=['-ldl']
 subprocess.run(cmd,check=True,env=env);subprocess.run([str(exe)],check=True,env=env,timeout=120)

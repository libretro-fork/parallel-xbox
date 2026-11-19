#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the actual UI device negotiation against deterministic CPU-only Vulkan stubs."""
from pathlib import Path
import os, shutil, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'ui/libretro.c').read_text()
def function(name):
 start=s.index('static bool '+name+'(');at=s.index('{',start);depth=1;end=at+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
start=s.index('#if !defined(__ANDROID__) && !defined(XEMU_DISABLE_READY_VK)')
block=s[start:s.index('static bool context_ready',start)]
driver=Path(__file__).with_suffix('.c').read_text().replace('/* OPTION_BLOCK */',block).replace('/* HAS_EXT */',function('xemu_vk_has_ext')).replace('/* DEVICE_FN */',function('xemu_vk_make_device'))
env=dict(os.environ,PATH=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+os.environ.get('PATH',''))
with tempfile.TemporaryDirectory(prefix='vk-object-negotiate-') as tmp:
 p=Path(tmp);src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-cast-function-type','-I'+str(r),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
 for label,flags in [('normal',[]),('Android',['-D__ANDROID__']),('disabled',['-DXEMU_DISABLE_READY_VK'])]:
  src.write_text(driver);subprocess.run(cmd+flags,env=env,check=True);subprocess.run([str(exe)],env=env,check=True,timeout=30);print(label,'PASS')
 for label,old,new in [('queried but not enabled dynamicRendering','.dynamicRendering = shader_object,','.dynamicRendering = VK_FALSE,'),('queried but not enabled shaderObject','.shaderObject = VK_TRUE,','.shaderObject = VK_FALSE,')]:
  assert driver.count(old)==1
  src.write_text(driver.replace(old,new));subprocess.run(cmd,env=env,check=True)
  result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
  assert result.returncode,label
  print('Required negative control rejected:',label)

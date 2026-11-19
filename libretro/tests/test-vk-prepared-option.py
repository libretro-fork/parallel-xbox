# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual default-off Vulkan frontend option and first-Vulkan restart latch."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
config=r/'hw/xbox/nv2a/pgraph/vk/ready-config.h'
if not config.is_file():print('Historical source has no prepared Vulkan option; no coverage claim');raise SystemExit(0)
s=(r/'ui/libretro.c').read_text();a=s.index('#if !defined(__ANDROID__) && !defined(XEMU_DISABLE_READY_VK)');z=s.index('static bool context_ready',a);block=s[a:z]
assert 'update_vk_prepared_vertex_option();' in s and 'latch_vk_prepared_vertex_option();' in s
code=Path(__file__).with_suffix('.c').read_text().replace('/* UI */',block)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-prepared-option-') as tmp:
 p=Path(tmp);src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r),'-I'+str(r/'libretro-common/include'),str(src),'-o',str(exe)]
 for label,flags in [('normal',[]),('Android',['-D__ANDROID__']),('disabled',['-DXEMU_DISABLE_READY_VK']),('GL-independent',['-DXEMU_DISABLE_READY_GL'])]:
  src.write_text(code);subprocess.run(cmd+flags,env=env,check=True);subprocess.run([str(exe)],env=env,check=True);print(label,'PASS')
 for label,text in [('wrong-renderer-latch',code.replace('if (use_vulkan && !vk_prepared_vertex.latched)','if (!use_vulkan && !vk_prepared_vertex.latched)',1)),('live-value-mutates-latch',code.replace('if (use_vulkan && !vk_prepared_vertex.latched)','if (use_vulkan)',1))]:
  assert text!=code;src.write_text(text);subprocess.run(cmd,env=env,check=True);bad=subprocess.run([str(exe)],env=env,capture_output=True);assert bad.returncode,label;print('Required negative control rejected:',label)

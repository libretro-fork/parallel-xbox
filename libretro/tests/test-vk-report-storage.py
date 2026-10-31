# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run production report polling against delayed query availability and a CPU oracle."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
s=(r/'hw/xbox/nv2a/pgraph/vk/reports.c').read_text();name='pgraph_vk_process_pending_reports_internal';a=s.index('void '+name+'(');b=s.index('{',a)+1;depth=1
while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
header=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text();a1=header.index('#define PGRAPH_VK_MAX_QUERIES');b1=header.index('} QueryReport;',a1)+len('} QueryReport;');storage=re.search(r'uint64_t query_results\[[^;]+;',header).group(0)
code=Path(__file__).with_suffix('.c').read_text().replace('/* REPORT TYPE */',header[a1:b1]).replace('/* RESULT STORAGE */',storage).replace('/* PRODUCTION */',s[a:b])
q=(r/'libretro/tests/test-spv-geom.c').read_text();a=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=q.index('int line_tests(',a);code=code.replace('/* QUIET ERRORS */',q[a:b])
with tempfile.TemporaryDirectory(prefix='vk-report-storage-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(d/'test.c'),'-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc','-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,timeout=30,env=env)

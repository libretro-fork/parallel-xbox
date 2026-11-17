#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the actual emitted range guard against independent IEEE CPU truth.

The ordinary static predicate is retained as a separate GPU control. Driver
compilation, readbacks and temporary buffers are correctness-only fixture work.
"""
from pathlib import Path
import importlib.util,os,subprocess,sys,json,tempfile,shutil
r=Path(sys.argv[1]).resolve();out=Path(tempfile.mkdtemp(prefix='light-range-gl-')).resolve()
spec=importlib.util.spec_from_file_location('fragment',r/'libretro/tests/test-psh-ready-gl.py');f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
geometry=(r/'libretro/tests/test-geom-dynamic-gl.c').read_text()
context=f.function(geometry,'static void quiet_abort(')+'\n'+f.function(geometry,'static void init_gl(')
source=(r/'hw/xbox/nv2a/pgraph/glsl/vsh-ff.c').read_text()
lines=[line.strip() for line in source.splitlines() if 'lightLocalRange(i)' in line and '{ continue; }' in line]
assert len(lines)==1,lines
import ast,re
emitted=ast.literal_eval(lines[0]);condition=re.search(r'if \((.*)\) \{ continue; \}',emitted).group(1).replace('lightLocalRange(i)','radiusValue')
static_lines=[line.split('/*',1)[0].strip() for line in source.splitlines() if 'if (d <= lightLocalRange(%d))' in line]
assert len(static_lines)==1,static_lines
static_emitted=ast.literal_eval(static_lines[0])
static_condition=re.search(r'if \((.*)\) \{',static_emitted).group(1).replace('lightLocalRange(%d)','radiusValue')
driver=(r/'libretro/tests/test-light-range-gl.c').read_text().replace('/* CONTEXT */',context).replace('/* ACTUAL_RANGE_GUARD */',json.dumps(condition)).replace('/* STATIC_RANGE_GUARD */',json.dumps(static_condition))
c=out/'range.c';c.write_text(driver);env=dict(os.environ,EGL_PLATFORM='surfaceless');cc=sys.argv[2] if len(sys.argv)>2 else 'cc';env['PATH']=str(Path(cc).parent)+os.pathsep+env.get('PATH','');exe=out/('range.exe' if os.name=='nt' else 'range')
flags=[cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen')];sources=[c]
if os.name=='nt':
 sources += [r/p for p in ['ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c','libretro/glib/gcontainers.c','libretro/glib/gcore.c']]
 flags+=['-I'+str(r/'libretro/glib')]
 sources += [r/'libretro-common'/p for p in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']];libs=['-lopengl32','-lgdi32','-luser32']
else:libs=['-lEGL','-lGL']
subprocess.run(flags+list(map(str,sources))+['-Wl,--gc-sections','-lm',*libs,'-o',str(exe)],env=env,check=True)
result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=60)
if result.returncode:print(result.stderr,file=sys.stderr)
assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
print(result.stdout,end='',flush=True)
rows=re.findall(r'RANGE mode=(\d+) checked=(\d+) differences=(\d+)',result.stdout)
assert len(rows)==7 and [int(a) for a,b,c in rows]==list(range(7)),rows
assert all(int(b)==576 and (int(c)==0 if int(a)<2 else int(c)>0) for a,b,c in rows),rows
print('PASS ordered light-range GPU truth: actual dynamic and static predicates, 1152 exact positive channels; five numerical fault controls rejected',flush=True)
assert out.parent==Path(tempfile.gettempdir()).resolve() and out.name.startswith('light-range-gl-')
shutil.rmtree(out)

# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import importlib.util,tempfile
spec=importlib.util.spec_from_file_location('scope',Path(__file__).with_name('shader_feature_scope.py'))
scope=importlib.util.module_from_spec(spec);spec.loader.exec_module(scope)
with tempfile.TemporaryDirectory(prefix='shader-feature-scope-') as tmp:
 root=Path(tmp)
 for path in ['hw/xbox/nv2a/pgraph/gl/renderer.h','hw/xbox/nv2a/pgraph/gl/shaders.c']:
  p=root/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('')
 gl=root/'hw/xbox/nv2a/pgraph/gl';tests=root/'libretro/tests';tests.mkdir(parents=True)
 files=[gl/'ready-ff.inc',gl/'ready-ff-state.h',*[tests/x for x in [
  'test-gl-ready-ff-owners.py','test-gl-ready-ff-owners.c','test-gl-ready-ff-source.py','test-gl-ready-ff-source.c','test-gl-ready-ff-unlit.py']]]
 cases=0
 for advertised in [False,True]:
  (gl/'renderer.h').write_text('ready_ff' if advertised else '')
  for mask in range(1<<len(files)):
   for bit,path in enumerate(files):
    if mask&(1<<bit):path.write_text('GL_READY_FF_STAGE_COUNT')
    elif path.exists():path.unlink()
   expected='historical' if not advertised and not(mask&3) else 'present' if mask==(1<<len(files))-1 else 'error'
   try:actual=scope.ready_ff_scope(root)
   except ValueError:actual='error'
   assert actual==expected,(advertised,mask,actual,expected)
   cases+=1
 (gl/'ready-ff-state.h').write_text('')
 try:scope.ready_ff_scope(root)
 except ValueError:cases+=1
 else:raise AssertionError('Missing advertised stage contract accepted')
 for path in files:path.unlink()
 (gl/'renderer.h').write_text('');(gl/'shaders.c').write_text('ready_ff')
 try:scope.ready_ff_scope(root)
 except ValueError:cases+=1
 else:raise AssertionError('Shader-side advertisement accepted without implementation')
 print('Feature scope:',cases,'historical/present/partial/advertised/missing-fixture cases PASS')

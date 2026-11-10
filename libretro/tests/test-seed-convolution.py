# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import os,subprocess,sys,tempfile,shlex
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
h=(r/'hw/xbox/nv2a/pgraph/glsl/psh.h').read_text()
a=h.index('static inline void pgraph_glsl_psh_migrate_seed(');b=h.index('\n}',a)+2;helper=h[a:b]
for banned in ('malloc','memcpy','realloc','free(','static const'):assert banned not in helper
# The migration is startup-only and precedes any hash/canonicalize/publication.
gl=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text(encoding='utf-8');vk=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text(encoding='utf-8')
compact=gl[gl.index('static unsigned seed_compact_dynamic_states('):gl.index('static void seed_load(')]
assert compact.index('pgraph_glsl_psh_migrate_seed(&state->psh);')<compact.index('pgraph_glsl_psh_canonicalize_dynamic(')<compact.index('fast_hash(')
load=gl[gl.index('static void seed_load('):gl.index('static void seed_finish(')]
assert 'if (key->kind == GL_FRAGMENT_SHADER)' in load and 'pgraph_glsl_psh_migrate_seed(&key->psh.state);' in load
assert load.index('pgraph_seed_read_first(')<load.index('pgraph_glsl_psh_migrate_seed(')<load.index('seed_compact_dynamic_states(')
load=vk[vk.index('static void vk_seed_siblings('):vk.index('static void vk_seed_step(')]
assert load.index('pgraph_seed_read(')<load.index('pgraph_glsl_psh_migrate_seed(&arr[i].psh);')<load.index('ShaderState fb = arr[i];')<load.index('fast_hash(')
assert gl.count('pgraph_glsl_psh_migrate_seed(')==2 and vk.count('pgraph_glsl_psh_migrate_seed(')==1
# Include the production helper in isolation so mutation controls cannot change
# any unrelated declaration or constructor.
template=Path(__file__).with_suffix('.c').read_text().replace('#include "hw/xbox/nv2a/pgraph/glsl/psh.h"','#include "fixture.h"')
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='seed-convolution-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 # The actual header is copied only as compiler source, not as a runtime payload.
 (d/'test.c').write_text(template);exe=d/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'hw/xbox/nv2a/pgraph/glsl')]
 runner=shlex.split(env.get('SEED_CONVOLUTION_RUNNER',''))
 def build(header):
  (d/'fixture.h').write_text(header);subprocess.run(flags+[str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 build(h);subprocess.run(runner+[str(exe)],check=True,env=env)
 for label,old,new in [('missing legacy2','state->conv_tex[i] == 2','state->conv_tex[i] == 99'),('changes current tags','state->conv_tex[i] == 1','state->conv_tex[i] <= 1'),('wrong kernel','state->conv_tex[i] = CONVOLUTION_FILTER_QUINCUNX;','state->conv_tex[i] = CONVOLUTION_FILTER_GAUSSIAN;')]:
  assert old in helper;build(h.replace(helper,helper.replace(old,new,1),1));result=subprocess.run(runner+[str(exe)],capture_output=True,env=env)
  assert result.returncode,label;print('Required seed-migration control rejected:',label)

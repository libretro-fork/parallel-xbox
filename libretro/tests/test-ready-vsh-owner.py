#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual experimental native owner and reject lifetime mutations."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(root/'hw/xbox/nv2a/pgraph/gl/ready-vsh.inc').read_text(encoding='utf-8')
shader=(root/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text(encoding='utf-8')
eviction=shader[shader.index('static void shader_cache_entry_post_evict('):]
assert eviction.index('ready_vsh_invalidate(r, binding);') < eviction.index('binding->initialized = false;')
template=Path(__file__).with_suffix('.c').read_text(encoding='utf-8')
env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name!='nt':
    import resource
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='ready-vsh-owner-') as temporary:
    directory=Path(temporary);c=directory/'test.c';exe=directory/('test.exe' if os.name=='nt' else 'test')
    def run(text):
        c.write_text(template.replace('/* PRODUCTION */',text),encoding='utf-8')
        subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                        '-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(c),'-o',str(exe)],check=True,env=env)
        return subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
    result=run(source);assert result.returncode==0,result.stderr;print(result.stdout.decode(),end='')
    for name,before,after in [
        ('unvalidated microcode','if (!pgraph_spv_vsh_supported(s, (SpvVshOptions){ .preserve_float32 = true }))','if (false)'),
        ('lost cache-owner invalidation','r->ready_vsh.uploaded_binding = NULL;','(void)r;'),
        ('linked shader object retained','glDetachShader(stage->gl_stage_program, shader);','(void)glDetachShader;'),
        ('wrong flags offset','GL_UNIFORM_BUFFER, VSH_INTERPRETER_FLAGS_OFFSET,','GL_UNIFORM_BUFFER, VSH_INTERPRETER_FLAGS_OFFSET + 4,'),
        ('wrong interface owner','key->vsh.glsl_opts.prefix_outputs ? 1u : 0u','key->vsh.glsl_opts.prefix_outputs ? 0u : 0u'),
    ]:
        assert before in source,name
        result=run(source.replace(before,after));assert result.returncode!=0,(name,result.stdout)
        print('Required owner/transport control rejected:',name)

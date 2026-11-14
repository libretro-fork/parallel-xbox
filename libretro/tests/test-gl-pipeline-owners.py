#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production native-pipeline ownership with all tuples colliding."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
source = (root/'hw/xbox/nv2a/pgraph/gl/pipeline-owners.inc').read_text(encoding='utf-8')
template = Path(__file__).with_suffix('.c').read_text(encoding='utf-8')
env = dict(os.environ)
env['PATH'] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='native-pipeline-owner-') as temporary:
    directory = Path(temporary)
    c = directory/'test.c'
    exe = directory/('test.exe' if os.name == 'nt' else 'test')
    def run(text):
        c.write_text(template.replace('/* PRODUCTION */', text), encoding='utf-8')
        subprocess.run([cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                        str(c), '-o', str(exe)], check=True, env=env)
        return subprocess.run([str(exe)], env=env, capture_output=True, timeout=30)
    result = run(source)
    assert result.returncode == 0, result.stderr
    print(result.stdout.decode(), end='')
    for name, before, after in [
        ('fragment identity lost', 'a->gl_prog_fs == b->gl_prog_fs', 'true'),
        ('geometry identity lost', 'a->gl_prog_gs == b->gl_prog_gs', 'true'),
        ('representative not transferred', '*index = replacement;', '(void)replacement;'),
        ('borrower ring not repaired', 'binding->pipeline_next->pipeline_prev = binding->pipeline_prev;', '(void)binding;'),
        ('borrowed pipeline deleted', '} else {\n        if (*index == binding)', '} else {\n        glDeleteProgramPipelines(1, &binding->gl_pipeline);\n        if (*index == binding)'),
    ]:
        assert before in source, name
        result = run(source.replace(before, after))
        assert result.returncode != 0, name
        print('Required pipeline ownership control rejected:', name)

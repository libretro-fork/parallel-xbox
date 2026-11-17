#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Supplementary exact proof for dynamic CSV with literal texgen.

Keep the original raw CSV/light inputs, all FF case bodies and comparisons.
Project dynamic fog onto both paths; pack only the literal texgen controls
independently. The complete unprojected corpus remains required separately.
"""
from pathlib import Path
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile


def prepare(root, directory, mutation=''):
    path = Path(__file__).with_name('test-gl-ready-ff-unlit.py')
    spec = importlib.util.spec_from_file_location('unlit', path)
    unlit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(unlit)
    unlit.prepare(root, directory)
    p = directory / 'driver.c'
    code = p.read_text()
    code = unlit.once(code, '  VshState excluded=state;', '''  if(state.is_fixed_function && state.fixed_function.csv0c_dynamic)state.fog_dynamic=true;
  VshState excluded=state;''')
    code = unlit.once(code, 'if(index<0 || state.fixed_function.csv0c_dynamic || state.fixed_function.lighting){outside++;continue;}',
                      'if(index<0 || !state.fixed_function.csv0c_dynamic || state.fixed_function.texgen_dynamic){outside++;continue;}')
    # CSV is already register-driven: retain its independent original values,
    # rather than reconstructing them from unused literal recipe fields.
    code = unlit.once(code, '  values.csv0cCtl[0]=(ff->local_eye?',
                      '  if(!ff->csv0c_dynamic)values.csv0cCtl[0]=(ff->local_eye?')
    code = unlit.once(code, 'Unlit FF domain:', 'CSV literal-texgen domain:')
    if mutation in ['point', 'skin']:
        changes = {
            'point': 'canonical.point_params_enable=!canonical.point_params_enable;',
            'skin': 'canonical.fixed_function.skinning=(canonical.fixed_function.skinning+1)%7;',
        }
        code = unlit.once(code, '  /* MUTATE_SOURCE */', changes[mutation])
    elif mutation:
        field, mask = {
            'csv': ('csv0cCtl', 1),
            'light': ('lightMode', 1),
            'texgen': ('texgenMode', 1),
            'matrix': ('texMatEnable', 1),
        }[mutation]
        needle = '   glUseProgram(program[path]);upload_uniforms(program[path],&values);'
        code = unlit.once(code, needle, needle + '\n   if(path)glUniform1ui(glGetUniformLocation(program[path],"' + field + '"),values.' + field + '[0]^' + str(mask) + 'u);')
    p.write_text(code)


def main():
    root = Path(sys.argv[1]).resolve()
    cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
    with tempfile.TemporaryDirectory(prefix='csv-literal-texgen-') as tmp:
        parent = Path(tmp)
        for mutation in ['', 'point', 'skin', 'csv', 'light', 'texgen', 'matrix']:
            directory = parent / (mutation or 'positive')
            directory.mkdir()
            prepare(root, directory, mutation)
            env = dict(os.environ, EGL_PLATFORM='surfaceless')
            env['PATH'] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + env.get('PATH', '')
            for field in ['SPV_VSH_GPU_RANGE', 'SPV_VSH_FIXTURE_ROUND', 'SPV_VSH_FIXTURE_ROUNDS']:
                env.pop(field, None)
            if mutation:
                env['SPV_VSH_GPU_RANGE'] = '32272:36368'
            with (directory / 'run.log').open('w') as stream:
                result = subprocess.run([sys.executable, str(directory / 'driver.py'), str(root), cc],
                                        env=env, stdout=stream, stderr=subprocess.STDOUT)
            log = (directory / 'run.log').read_text(errors='replace')
            if not mutation:
                assert result.returncode == 0, log[-4000:]
                rows = re.findall(r'CSV literal-texgen domain: (\d+) eligible, (\d+) outside; exact checks=(\d+)', log)
                assert rows and sum(int(a) + int(b) for a, b, c in rows) == 33232, rows
                count = sum(int(a) for a, b, c in rows)
                checks = sum(int(c) for a, b, c in rows)
                assert count == 400 and checks == 652800, (count, checks)
                print('PASS CSV literal texgen: full 33232-case domain ledger; 400 eligible / 652800 exact scalar checks; original CSV/light controls retained', flush=True)
            else:
                assert result.returncode != 0 and re.search(r'[1-9][0-9]* differences;', log), (mutation, result.returncode, log[-4000:])
                print('Required CSV literal-texgen fault rejected:', mutation, flush=True)


if __name__ == '__main__':
    main()

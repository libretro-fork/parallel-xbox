#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact GL proof for material-sourced lighting with no active lights.

Add the same dynamic-fog/material/light projection to both paths. This
supplements the original corpus; active-light and literal-fog conversion
remain excluded. Runtime sources, owners and upload ABI are unchanged.
"""
from pathlib import Path
import importlib.util, os, re, shutil, subprocess, sys, tempfile


def prepare(root, directory, mutation=''):
    path = Path(__file__).with_name('test-gl-ready-ff-unlit.py')
    spec = importlib.util.spec_from_file_location('unlit', path)
    unlit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(unlit)
    unlit.prepare(root, directory)
    p = directory / 'driver.c'
    code = p.read_text()
    code = unlit.once(code, '  VshState excluded=state;', '''  if(state.is_fixed_function && state.fixed_function.lighting && !state.fixed_function.csv0c_dynamic){
   state.fog_dynamic=true;state.fixed_function.light_dynamic=false;
   memset(state.fixed_function.light,0,sizeof(state.fixed_function.light));
   state.fixed_function.emission_src=state.fixed_function.ambient_src=MATERIAL_COLOR_SRC_MATERIAL;
   state.fixed_function.diffuse_src=state.fixed_function.specular_src=MATERIAL_COLOR_SRC_MATERIAL;
  }
  VshState excluded=state;''')
    code = unlit.once(code, '|| state.fixed_function.lighting){outside++;continue;}',
                      '|| !state.fixed_function.lighting){outside++;continue;}')
    code = unlit.once(code, 'values.csv0cCtl[0]=(ff->local_eye?',
                      'values.csv0cCtl[0]=1u|(ff->local_eye?')
    code = unlit.once(code, '  if(!ff->texgen_dynamic){',
                      '  values.lightMode[0]=0; /* Independent no-active-light input. */\n  if(!ff->texgen_dynamic){')
    code = unlit.once(code, 'Unlit FF domain:', 'Idle-lit FF domain:')
    if mutation:
        needle = '   glUseProgram(program[path]);upload_uniforms(program[path],&values);'
        changes = {
            'master': 'glUniform1ui(glGetUniformLocation(program[path],"csv0cCtl"),values.csv0cCtl[0]^1u);',
            'material': 'glUniform1ui(glGetUniformLocation(program[path],"csv0cCtl"),values.csv0cCtl[0]^64u);',
            'light': 'glUniform1ui(glGetUniformLocation(program[path],"lightMode"),1u|(1u<<16));',
        }
        code = unlit.once(code, needle, needle + '\n   if(path){' + changes[mutation] + '}')
    p.write_text(code)


def main():
    root = Path(sys.argv[1]).resolve()
    cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
    with tempfile.TemporaryDirectory(prefix='idle-lit-ff-') as tmp:
        parent = Path(tmp)
        for mutation in ['', 'master', 'material', 'light']:
            directory = parent / (mutation or 'positive')
            directory.mkdir()
            prepare(root, directory, mutation)
            env = dict(os.environ, EGL_PLATFORM='surfaceless')
            env['PATH'] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + env.get('PATH', '')
            for field in ['SPV_VSH_GPU_RANGE', 'SPV_VSH_FIXTURE_ROUND', 'SPV_VSH_FIXTURE_ROUNDS']:
                env.pop(field, None)
            if mutation:
                env['SPV_VSH_GPU_RANGE'] = '20016:20032'
            with (directory / 'run.log').open('w') as stream:
                result = subprocess.run([sys.executable, str(directory / 'driver.py'), str(root), cc],
                                        env=env, stdout=stream, stderr=subprocess.STDOUT)
            log = (directory / 'run.log').read_text(errors='replace')
            if not mutation:
                assert result.returncode == 0, log[-4000:]
                rows = re.findall(r'Idle-lit FF domain: (\d+) eligible, (\d+) outside; exact checks=(\d+)', log)
                assert rows and sum(int(a) + int(b) for a, b, c in rows) == 33232, rows
                count = sum(int(a) for a, b, c in rows)
                checks = sum(int(c) for a, b, c in rows)
                assert count == 2464 and checks == 4021248, (count, checks)
                print('PASS idle-lit FF: full 33232-case domain ledger; 2464 eligible / 4021248 exact scalar checks; active lights and literal fog retain fallback', flush=True)
            else:
                assert result.returncode != 0 and re.search(r'[1-9][0-9]* differences;', log), (mutation, result.returncode, log[-4000:])
                print('Required idle-lit FF fault rejected:', mutation, flush=True)


if __name__ == '__main__':
    main()

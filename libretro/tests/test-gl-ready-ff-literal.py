#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact ordinary-GL proof for the literal material-lighting startup family.

Retain every original FF case body and numeric comparison. This supplementary
domain projects the admitted literal controls and dynamic fog onto BOTH paths;
light modes, lighting inputs, skinning, points and texgen remain varied. The
original unprojected GL corpus and the older prepared-family tests still run.
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
    code = unlit.once(code, '  VshState excluded=state;', '''  if(state.is_fixed_function && state.fixed_function.lighting && !state.fixed_function.csv0c_dynamic){
   state.fog_dynamic=true;
   state.fixed_function.normalization=state.fixed_function.local_eye=state.fixed_function.two_sided=false;
   state.specular_enable=state.separate_specular=state.ignore_specular_alpha=true;
   state.fixed_function.emission_src=state.fixed_function.ambient_src=MATERIAL_COLOR_SRC_MATERIAL;
   state.fixed_function.diffuse_src=state.fixed_function.specular_src=MATERIAL_COLOR_SRC_MATERIAL;
  }
  VshState excluded=state;''')
    code = unlit.once(code, '|| state.fixed_function.lighting){outside++;continue;}',
                      '|| !state.fixed_function.lighting){outside++;continue;}')
    code = unlit.once(code, 'assert(ready_count<56);',
                      'assert(ready_count<GL_READY_FF_STAGE_COUNT);')
    code = unlit.once(code, 'values.csv0cCtl[0]=(ff->local_eye?',
                      'values.csv0cCtl[0]=1u|(ff->local_eye?')
    code = unlit.once(code, '  if(!ff->texgen_dynamic){', '''  if(!ff->light_dynamic){
   /* Independent packing of the static recipe's eight hardware light modes. */
   uint32_t modes=0,count=0;
   for(unsigned light=0;light<8;light++){
    modes|=(unsigned)ff->light[light]<<(2*light);
    if(ff->light[light]!=LIGHT_OFF)count=light+1;
   }
   values.lightMode[0]=modes|(count<<16);
  }
  if(!ff->texgen_dynamic){''')
    code = unlit.once(code, 'Unlit FF domain:', 'Literal-lit FF domain:')
    if mutation in ['material', 'normal', 'eye', 'back', 'separate', 'skin']:
        changes = {
            'material': 'canonical.fixed_function.emission_src=MATERIAL_COLOR_SRC_DIFFUSE;',
            'normal': 'canonical.fixed_function.normalization=true;',
            'eye': 'canonical.fixed_function.local_eye=true;',
            'back': 'canonical.fixed_function.two_sided=true;',
            'separate': 'canonical.separate_specular=false;',
            'skin': 'canonical.fixed_function.skinning=(canonical.fixed_function.skinning+1)%7;',
        }
        code = unlit.once(code, '  /* MUTATE_SOURCE */', changes[mutation])
    elif mutation:
        changes = {
            'light': 'glUniform1ui(glGetUniformLocation(program[path],"lightMode"),0u);',
            'matrix': 'glUniform1ui(glGetUniformLocation(program[path],"texMatEnable"),values.texMatEnable[0]^1u);',
        }
        needle = '   glUseProgram(program[path]);upload_uniforms(program[path],&values);'
        code = unlit.once(code, needle, needle + '\n   if(path){' + changes[mutation] + '}')
    p.write_text(code)


def main():
    root = Path(sys.argv[1]).resolve()
    cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
    with tempfile.TemporaryDirectory(prefix='literal-lit-ff-') as tmp:
        parent = Path(tmp)
        for mutation in ['', 'material', 'normal', 'eye', 'back', 'separate', 'skin', 'light', 'matrix']:
            directory = parent / (mutation or 'positive')
            directory.mkdir()
            prepare(root, directory, mutation)
            env = dict(os.environ, EGL_PLATFORM='surfaceless')
            env['PATH'] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + env.get('PATH', '')
            for field in ['SPV_VSH_GPU_RANGE', 'SPV_VSH_FIXTURE_ROUND', 'SPV_VSH_FIXTURE_ROUNDS']:
                env.pop(field, None)
            if mutation:
                # The later interval uses a constant, saturated specular curve;
                # the original earlier cases expose the eye-dependent half dot.
                env['SPV_VSH_GPU_RANGE'] = '20016:20032' if mutation == 'eye' else '20096:20112'
            with (directory / 'run.log').open('w') as stream:
                result = subprocess.run([sys.executable, str(directory / 'driver.py'), str(root), cc],
                                        env=env, stdout=stream, stderr=subprocess.STDOUT)
            log = (directory / 'run.log').read_text(errors='replace')
            if not mutation:
                assert result.returncode == 0, log[-4000:]
                rows = re.findall(r'Literal-lit FF domain: (\d+) eligible, (\d+) outside; exact checks=(\d+)', log)
                assert rows and sum(int(a) + int(b) for a, b, c in rows) == 33232, rows
                count = sum(int(a) for a, b, c in rows)
                checks = sum(int(c) for a, b, c in rows)
                assert count == 2464 and checks == 4021248, (count, checks)
                print('PASS literal-lit FF: full 33232-case domain ledger; 2464 eligible / 4021248 exact scalar checks; original controls/corpus retained separately', flush=True)
            else:
                assert result.returncode != 0 and re.search(r'[1-9][0-9]* differences;', log), (mutation, result.returncode, log[-4000:])
                print('Required literal-lit FF fault rejected:', mutation, flush=True)


if __name__ == '__main__':
    main()

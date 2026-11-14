#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual GL interpreter vs specialized GLSL, with a separate code UBO.

The production builder, existing programmable corpus and exact comparison
rules are shared. Readbacks, per-case allocations and the 128-entry source
lookup are test-only correctness machinery, not a runtime fallback design.
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
    path = Path(__file__).with_name('test-vsh-interpreter-gpu.py')
    spec = importlib.util.spec_from_file_location('interpreter_fixture', path)
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    fixture.prepare(root, directory, mutation)
    original = (root / 'libretro/tests/test-spv-vsh-gpu.c').read_text()
    corpus = original[original.index(' for(unsigned test=first;'):original.index('  GlslcResult modules[2];')]
    needle = 'opts.preserve_float32=fp.shaderSignedZeroInfNanPreserveFloat32;'
    assert corpus.count(needle) == 1
    corpus = corpus.replace(needle, '/* Native GLSL has no Vulkan execution mode. */')
    line = next(line for line in corpus.splitlines() if line.startswith('  for(unsigned a=0;a<16;a++)attrs[a].format='))
    corpus = corpus.replace(line, '')
    pieces = {
        'TOKEN_HELPERS': original[original.index('static void token('):original.index('static void pack(')],
        'KEY_HELPERS': original[original.index('/* Test-only guard'):original.index('int main(')],
        'CORPUS': corpus,
        'UNIFORM_VALUES': original[original.index('  VshUniformValues values='):original.index('  for(unsigned path=0;path<2;path++) {')],
        'COMPARE': original[original.index('   if(test>=39440 && test<40464 && ((test-39440)/64)'):original.index('   vkDestroyFence(device,fence,NULL);')],
    }
    code = Path(__file__).with_suffix('.c').read_text()
    for name, body in pieces.items():
        marker = '/* ' + name + ' */'
        assert code.count(marker) == 1
        code = code.replace(marker, body)
    if mutation == 'code_upload':
        needle = 'glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(values.vpCode),values.vpCode);'
        assert code.count(needle) == 1
        code = code.replace(needle, 'memset(values.vpCode,0,sizeof(values.vpCode));' + needle)
    (directory / 'driver.c').write_text(code)

    script = (directory / 'driver.py').read_text()
    needle = "'-I'+str(d),"
    assert script.count(needle) == 1
    script = script.replace(needle, "'-I'+str(d),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen'),")
    needle = " sources += [r/'libretro/glib/gcontainers.c']"
    assert script.count(needle) == 1
    extra = ['ui/gl-dispatch.c', 'libretro-common/glsym/rglgen.c',
             'libretro-common/glsym/glsym_gl.c',
             'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c']
    script = script.replace(needle, ' sources += [r/x for x in ' + repr(extra) + ']\n' + needle)
    needle = "['-lvulkan']"
    assert script.count(needle) == 1
    script = script.replace(needle, "['-lEGL','-lGL']")
    needle = "str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')"
    assert script.count(needle) == 1
    script = script.replace(needle, "'-lopengl32','-lgdi32','-luser32'")
    (directory / 'driver.py').write_text(script)


def run(root, compiler, directory, mutation='', case=None):
    prepare(root, directory, mutation)
    environment = dict(os.environ, EGL_PLATFORM='surfaceless')
    executable = shutil.which(compiler) or compiler
    environment['PATH'] = str(Path(executable).resolve().parent) + os.pathsep + environment.get('PATH', '')
    if case is not None:
        environment['SPV_VSH_GPU_RANGE'] = str(case) + ':' + str(case + 1)
    log = directory / 'result.log'
    with log.open('w') as stream:
        result = subprocess.run([sys.executable, str(directory / 'driver.py'), str(root), compiler],
                                env=environment, stdout=stream, stderr=subprocess.STDOUT)
    text = log.read_text(errors='replace')
    if not mutation:
        print(text, end='', flush=True)
        assert result.returncode == 0, result.returncode
    elif mutation == 'compile_program':
        assert result.returncode != 0 and 'Assertion' in text and '!strcmp(mstring_get_str(interpreted),mstring_get_str(different))' in text, text
    else:
        mismatch = re.search(r'[1-9][0-9]* differences;', text)
        assert result.returncode != 0 and mismatch, text
        print('Control evidence:', mutation, mismatch.group(0), flush=True)
    if mutation:
        print('Required GL interpreter control rejected:', mutation, flush=True)
    return text


def main():
    root = Path(sys.argv[1]).resolve()
    compiler = sys.argv[2] if len(sys.argv) > 2 else 'cc'
    with tempfile.TemporaryDirectory(prefix='vsh-interpreter-gl-') as temporary:
        directory = Path(temporary)
        positive = directory / 'positive'
        positive.mkdir()
        device = run(root, compiler, positive)
        for mutation, case in [('shared_c', 31), ('mirror_fold', 589),
                               ('lit_fold', 659), ('lit_result', 659),
                               ('final_bit', 0), ('compile_program', 0),
                               ('point_literal', 1582), ('point_written', 1709),
                               ('code_upload', 0)]:
            if mutation == 'lit_fold' and 'llvmpipe' not in device:
                print('Literal-fold control requires llvmpipe; LIT-result remains required here', flush=True)
                continue
            folder = directory / mutation
            folder.mkdir()
            run(root, compiler, folder, mutation, case)


if __name__ == '__main__':
    main()

# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute local-initializer semantics on GPU; optionally reject an old compiler."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo', type=Path)
parser.add_argument('--cc', default='gcc')
parser.add_argument('--baseline', help='Git revision to run as a negative control')
args = parser.parse_args()
repo = args.repo.resolve()
helpers = (repo / 'libretro/tests/spv-clear-vulkan.h').read_text().split('static VkPipeline pipeline(')[0]
source = Path(__file__).with_suffix('.c').read_text().replace('/* GPU HELPERS */', helpers)
env = os.environ.copy()
env['PATH'] = str(Path(args.cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='glsl-local-') as directory:
    directory = Path(directory)
    test = directory / 'test.c'; test.write_text(source)
    for baseline in (False, True) if args.baseline else (False,):
        compiler = repo / 'hw/xbox/nv2a/pgraph/spv/glslc.c'
        if baseline:
            compiler = directory / 'baseline.c'
            compiler.write_bytes(subprocess.check_output(['git', '-C', str(repo), 'show',
                args.baseline + ':hw/xbox/nv2a/pgraph/spv/glslc.c']))
        exe = directory / ('test.exe' if os.name == 'nt' else 'test')
        command = [args.cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-Wno-missing-field-initializers', '-Wno-unused-variable',
            '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
            '-I' + str(repo), '-I' + str(repo / 'deps/volk'),
            '-I' + str(repo / 'deps/vulkan-headers'), '-I' + str(repo / 'deps/spirv-headers/include'),
            '-I' + str(repo / 'hw/xbox/nv2a/pgraph/spv'), str(test), str(compiler),
            str(repo / 'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'), str(repo / 'deps/volk/volk.c'),
            '-lm', '-o', str(exe)]
        if os.name != 'nt': command += ['-ldl']
        subprocess.run(command, check=True, env=env)
        result = subprocess.run([str(exe)], env=env, timeout=60)
        if baseline:
            assert result.returncode == 1, 'negative control must fail the GPU value checks'
            print('Pre-fix compiler rejected by GPU value checks')
        else:
            result.check_returncode()

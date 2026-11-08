# SPDX-License-Identifier: GPL-2.0-or-later
"""Read back depth from the opt-in caster pipeline using existing GPU buffers."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo', type=Path)
parser.add_argument('--cc', default='gcc')
args = parser.parse_args()
repo = args.repo.resolve()
helpers = (repo / 'libretro/tests/spv-clear-vulkan.h').read_text().split('static VkPipeline pipeline(')[0]
source = Path(__file__).with_suffix('.c').read_text().replace('/* GPU HELPERS */', helpers)
env = os.environ.copy()
env['PATH'] = str(Path(args.cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='sun-vulkan-') as directory:
    directory = Path(directory)
    test = directory / 'test.c'; test.write_text(source)
    exe = directory / ('test.exe' if os.name == 'nt' else 'test')
    command = [args.cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-Wno-missing-field-initializers',
               '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
               '-I' + str(repo), '-I' + str(repo / 'deps/volk'),
               '-I' + str(repo / 'libretro/tests'),
               '-I' + str(repo / 'libretro-common/include'),
               '-I' + str(repo / 'deps/vulkan-headers'),
               '-I' + str(repo / 'deps/spirv-headers/include'), str(test),
               str(repo / 'hw/xbox/nv2a/pgraph/spv/glslc.c'),
               str(repo / 'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),
               str(repo / 'deps/volk/volk.c'), '-lm', '-o', str(exe)]
    if os.name != 'nt': command += ['-ldl']
    subprocess.run(command, check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env, timeout=120)

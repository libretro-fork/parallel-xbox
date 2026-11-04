#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise actual sibling queue policy with real incoming SPSC storage."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
source = (repo / 'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text(encoding='utf-8')
header = (repo / 'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8')
def function(name):
    m = re.search(r'^static (?:inline QEMU_ALWAYS_INLINE )?void ' + name + r'\([^;]*?\)\s*\{', source, re.M)
    assert m, name
    a = source.index('{', m.start())
    b, depth = a + 1, 1
    while depth:
        depth += (source[b] == '{') - (source[b] == '}')
        b += 1
    return source[m.start():b]
view = source[source.index('enum {\n    VK_SHADER_HEAD_BYTES'):source.index('static inline int vk_pre_state_module_keys(')]
key = re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;', header, re.S)[0]
record = re.search(r'typedef struct VkSpecDone \{.*?\} VkSpecDone;', source, re.S)[0]
reference = Path(__file__).with_name('sibling-queue-reference.h').read_text(encoding='utf-8')
functions = reference + '\n' + '\n'.join(function(n) for n in ('vk_spec_queue_impl', 'vk_spec_queue', 'vk_spec_queue_view'))
code = Path(__file__).with_suffix('.c').read_text(encoding='utf-8').replace('/* TYPES */', key + '\n' + record + '\n' + view).replace('/* FUNCTIONS */', functions)
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='sibling-queue-') as tmp:
    p = Path(tmp)
    (p / 'qemu').mkdir()
    (p / 'qemu/osdep.h').write_bytes(b'#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
    (p / 'qemu/mstring.h').write_bytes(b'typedef struct MString MString;\n')
    (p / 'test.c').write_bytes(code.encode())
    exe = p / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                    '-I' + str(p), '-I' + str(repo), '-I' + str(repo / 'include'),
                    '-I' + str(repo / 'libretro-common/include'),
                    '-I' + str(repo / 'deps/vulkan-headers'), str(p / 'test.c'),
                    str(repo / 'libretro-common/queues/retro_spsc.c'), '-o', str(exe)], env=env, check=True)
    subprocess.run([str(exe)], env=env, check=True, timeout=120)

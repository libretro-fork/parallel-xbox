# SPDX-License-Identifier: GPL-2.0-or-later
"""Production fence publication/read with real libretro-common atomics/threads."""
from pathlib import Path
import os, re, shlex, shutil, subprocess, sys, tempfile

repo = Path(sys.argv[1]).resolve()
cc = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')

def function(source, name):
    match = re.search(r'^(?:static inline )?(?:uint32_t|uint64_t|void) ' + name + r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

header = (repo / 'hw/xbox/nv2a/pgraph/pgraph.h').read_text()
source = (repo / 'hw/xbox/nv2a/pgraph/pgraph.c').read_text()
method = source.split('case NV044_SET_MONOCHROME_COLOR0:', 1)[1].split('break;', 1)[0]
assert 'pgraph_fence_publish(pg, parameter);' in method
production = '\n'.join(function(header, n) for n in ('pgraph_reg_r', 'pgraph_reg_w', 'pgraph_fence_publish'))
production += '\n' + function(source, 'pgraph_read')
fixture = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', production)
with tempfile.TemporaryDirectory(prefix='pgraph-fence-') as directory:
    work = Path(directory)
    c = work / 'test.c'
    c.write_text(fixture)
    options = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare',
               '-DHAVE_THREADS', '-I' + str(repo), '-I' + str(repo / 'libretro-common/include')]
    binary = os.environ.get('PGRAPH_FENCE_BINARY', str(work / 'test'))
    providers = [str(repo / 'libretro-common/rthreads/rthreads.c')]
    if 'mingw' in cc[0]:
        options += ['-Wno-cast-function-type']
        providers += [str(repo / p) for p in ('libretro-common/compat/compat_strl.c','libretro-common/features/features_cpu.c', 'libretro-common/time/rtime.c')]
    subprocess.run(cc + options + [str(c)] + providers + ['-pthread', '-o', binary], check=True)
    if not os.environ.get('PGRAPH_FENCE_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
    # Use the real portable atomic header in every code-generation check.
    for compiler, arm in [(cc, False)] + ([(['aarch64-linux-gnu-gcc'], True)] if shutil.which('aarch64-linux-gnu-gcc') else []):
        assembly = work / ('arm.s' if arm else 'native.s')
        subprocess.run(compiler + options + ['-S', str(c), '-o', str(assembly)], check=True)
        text = assembly.read_text()
        if arm:
            poll = text.split('pgraph_read:', 1)[1].split('.size\tpgraph_read', 1)[0]
            publish = text.split('probe_publish:', 1)[1].split('.size\tprobe_publish', 1)[0]
            assert re.search(r'\bldar\b', poll) and re.search(r'\bstlr\b', publish)
            assert not re.search(r'\b(dmb|ldaxr|stlxr)\b', poll + publish)
            assert not re.search(r'\[sp(?:,|\])', publish)
            print('PGRAPH fence AArch64: acquire LDAR / release STLR, no full barrier or exclusive retry')
        elif '__x86_64__' in subprocess.run(compiler + ['-dM', '-E', '-'], input='', text=True, capture_output=True, check=True).stdout:
            publish = text.split('probe_publish:', 1)[1].split('.size\tprobe_publish', 1)[0] if '.size\tprobe_publish' in text else text.split('probe_publish:', 1)[1].split('.seh_endproc', 1)[0]
            assert not re.search(r'\b(lock|mfence|lfence|sfence|call)\b', publish)
            assert '(%rsp)' not in publish and '(%rbp)' not in publish
            print('PGRAPH fence x86-64: inline publication without locked instruction, fence or helper call')

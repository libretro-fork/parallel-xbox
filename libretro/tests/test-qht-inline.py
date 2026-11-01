# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production inline QHT reader with native retro atomics."""
from pathlib import Path
import os, subprocess, sys, tempfile
r = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
def function(path, signature):
    s = (r / path).read_text(); a = s.index(signature); b = s.index('{', a) + 1; depth = 1
    while depth:
        depth += (s[b] == '{') - (s[b] == '}'); b += 1
    return s[a:b]
seq = (r / 'include/qemu/seqlock.h').read_text()
seq = seq[seq.index('struct QemuSeqLock {'):seq.index('/* Lock out other writers and update the count.  */', seq.index('static inline void seqlock_write_end'))]
seq += function('include/qemu/seqlock.h', 'static inline unsigned seqlock_read_begin(')
seq += function('include/qemu/seqlock.h', 'static inline int seqlock_read_retry(')
slow = function('util/qht.c', '__attribute__((noinline))')
wrapper = function('util/qht.c', 'void *qht_lookup_custom(')
source = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', seq + '\n#include "qemu/qht-lookup.h"\n' + slow + '\n' + wrapper)
with tempfile.TemporaryDirectory(prefix='qht-inline-') as d:
    p = Path(d); (p/'qemu').mkdir()
    (p/'qemu/qht.h').write_text('typedef bool (*qht_lookup_func_t)(const void *,const void *); struct qht {retro_atomic_ptr_t map;};\n')
    (p/'qemu/rcu.h').write_text('struct rcu_head {void *next; void (*func)(void *);};\n')
    (p/'test.c').write_text(source)
    flags = [cc, '-O2', '-std=gnu11', '-pthread', '-Wall', '-Wextra', '-Werror', '-I'+str(p), '-I'+str(r/'include'), '-I'+str(r/'libretro-common/include')]
    if os.environ.get('QHT_SANITIZE'): flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(flags+[str(p/'test.c'), '-o', str(p/'test.exe')], check=True)
    subprocess.run([str(p/'test.exe')], check=True)

    if os.name != 'nt':
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        header = (r/'include/qemu/qht-lookup.h').read_text()
        check = 'if (likely(!seqlock_read_retry(&b->sequence, version)))'
        assert check in header
        (p/'qemu/qht-lookup.h').write_text(header.replace(check, 'if ((void)version, 1)', 1))
        subprocess.run(flags+['-Wno-unused-function', str(p/'test.c'), '-o', str(p/'negative')], check=True)
        result = subprocess.run([str(p/'negative')], capture_output=True)
        assert result.returncode != 0 and b'Assertion' in result.stderr
        print('Negative control rejected: omitting the retry returns a stale successful lookup')

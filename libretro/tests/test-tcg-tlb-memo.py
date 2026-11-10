#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual x86-64 TLB memo emitter against independent bounds checks."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
if os.name != 'nt':
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
cc = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
macros = subprocess.run(cc + ['-dM', '-E', '-'], input='', text=True,
                        capture_output=True, check=True).stdout
if '__x86_64__' not in macros:
    print('TLB memo execution fixture: skipped (requires an x86-64 host)')
    raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix='tcg-tlb-memo-') as directory:
    folder = Path(directory)
    binary = Path(os.environ.get('TCG_TLB_MEMO_BINARY', folder / 'test'))
    flags = ['-I' + str(folder), '-O2', '-std=gnu11', '-fno-strict-aliasing', '-fwrapv', '-mcx16',
             '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
             '-D_GNU_SOURCE', '-DHAVE_THREAD_STORAGE', '-DCONFIG_SOFTMMU',
             '-DCOMPILING_SYSTEM_VS_USER']
    for path in ['libretro/gen', '.', 'libretro/glib', 'libretro-common/include',
                 'include', 'host/include/x86_64', 'host/include/generic', 'tcg', 'tcg/i386']:
        flags += ['-I' + str(r / path)]
    flags += shlex.split(os.environ.get('TCG_TLB_MEMO_CFLAGS', ''))
    if os.name == 'nt' or 'mingw' in ' '.join(cc):
        # PE does not discard all of tcg.c's unused public functions like ELF
        # section GC does. Privatize them in this single-TU test executable.
        flags += ['-flto', '-fwhole-program']
    subprocess.run(cc + flags + [str(Path(__file__).with_suffix('.c')), '-o', str(binary)], check=True)
    if not os.environ.get('TCG_TLB_MEMO_SKIP_RUN'):
        subprocess.run([str(binary)], check=True, timeout=60)
        if os.environ.get('TCG_TLB_MEMO_NEGATIVE'):
            backend = (r / 'tcg/i386/tcg-target.c.inc').read_text()
            plan = (r / 'tcg/tcg-tlbm.c.inc').read_text()
            core = (r / 'tcg/tcg.c').read_text()
            guard = backend.index('static void tlbm_out_share(')
            before = backend[:guard]
            after = backend[guard:]
            assert 'if (!is_ld) {' in after
            controls = [
                ('dirty store check omitted', 'tcg-target.c.inc', before + after.replace('if (!is_ld) {', 'if (false) {', 1)),
                ('zero used as invalid tag', 'tcg-target.c.inc', backend.replace('#define TLBM_NONE     0x80000000u', '#define TLBM_NONE 0')),
                ('MMU identity ignored', 'tcg-tlbm.c.inc', plan.replace(' || f->mmu != x->mmu', '')),
                ('helper barrier omitted', 'tcg-tlbm.c.inc', plan.replace('seg++;\n            memset(vroot, 0, sizeof(int) * s->nb_globals);', 'memset(vroot, 0, sizeof(int) * s->nb_globals);')),
                ('ABI argument registers stay reserved', 'tcg/tcg.c', core.replace('allocated_regs &= ~tlbm_call_regs;', '(void)allocated_regs;')),
            ]
            if '-DCONFIG_DEBUG_TCG' in flags:
                controls.append(('debug addend disagreement', 'tcg-target.c.inc', backend.replace(
                    '    tlbm_out_check(s, addr, oi, is_ld,',
                    '    tgen_arithi(s, ARITH_ADD + P_REXW, TCG_REG_R11, 1, 0);\n    tlbm_out_check(s, addr, oi, is_ld,')))
            for name, filename, content in controls:
                original = {'tcg-target.c.inc': backend, 'tcg-tlbm.c.inc': plan,
                            'tcg/tcg.c': core}[filename]
                assert content != original, name
                # tcg.c finds the planner in its own directory; a private copy
                # lets both backend and planner overrides use the include path.
                (folder / 'tcg').mkdir(exist_ok=True)
                (folder / 'tcg/tcg.c').write_text(core)
                override = folder / filename
                override.write_text(content)
                subprocess.run(cc + flags + [str(Path(__file__).with_suffix('.c')), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
                assert result.returncode, 'negative control escaped: ' + name
                if name == 'debug addend disagreement':
                    assert b'TLB entry changed under a TLB memo' in result.stdout
                override.unlink()
                print('Required negative control rejected:', name, flush=True)

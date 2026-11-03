# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production ACI MMIO interrupt/status/control and reset paths."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
root = Path(sys.argv[1]).resolve()
source = (root / 'hw/xbox/mcpx/aci.c').read_text()
def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
constants = source[source.index('#define SR_FIFOE'):source.index('/* codec registers */')]
registers = source[source.index('/* bus masters */'):source.index('/* PCM out pacing:')]
types = source[source.index('typedef struct BD {'):source.index('typedef struct MCPXACIState {')]
production = '\n'.join(function(name) for name in (
    'static void update_sr(', 'static void reset_bm_regs(', 'static void reset_bus_masters(',
    'static void nabm_writeb(', 'static void nabm_writew(', 'static void nabm_writel('))
assert 'reset_bus_masters(d);' in function('static void mcpx_aci_realize(')
fixture = Path(__file__).with_suffix('.c').read_text()
code = fixture.replace('/* CONSTANTS */', constants).replace('/* REGISTERS */', registers).replace('/* TYPES */', types).replace('/* PRODUCTION */', production)
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
runner = shlex.split(os.environ.get('ACI_IRQ_RUNNER', ''))
flags = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror']
flags += shlex.split(os.environ.get('ACI_IRQ_CFLAGS', '-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
mutations = {
    'shared-line': ('if (s->glob_sta & (GS_PIINT | GS_POINT | GS_MINT | GS_SOINT))', 'if (pending)'),
    'control-enables': ('/* Enables can change while the interrupt status stays set. */\n            update_sr(s, r, r->sr);', '/* missing enable update */'),
    'fifo-enable': (' ||\n                   ((new_sr & SR_FIFOE) && (r->cr & CR_FEIE))', ''),
    'spdif-reset': ('    reset_bm_regs(s, &s->bm_regs[SO_INDEX]);', '    /* missing S/PDIF reset */'),
}
with tempfile.TemporaryDirectory(prefix='aci-irq-') as temporary:
    path = Path(temporary)
    for name in ('production', *mutations):
        test = code
        if name in mutations:
            old, new = mutations[name]
            assert test.count(old) == 1, name
            test = test.replace(old, new, 1)
        c = path / 'test.c'; exe = path / 'test.exe'
        c.write_text(test)
        subprocess.run(compiler + flags + [str(c), '-o', str(exe)], check=True)
        result = subprocess.run(runner + [str(exe)], capture_output=True, timeout=30)
        if name == 'production':
            sys.stdout.buffer.write(result.stdout)
            if result.returncode: sys.stderr.buffer.write(result.stderr)
            result.check_returncode()
        else:
            assert result.returncode and b'ACI IRQ:' in result.stderr, (name, result.stderr)
            print('Negative control rejected:', name)

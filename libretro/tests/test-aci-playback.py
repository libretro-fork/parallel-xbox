# SPDX-License-Identifier: GPL-2.0-or-later
"""Run production ACI timers, descriptor walking and MMIO against a virtual clock."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
root = Path(sys.argv[1]).resolve()
source = (root / 'hw/xbox/mcpx/aci.c').read_text()
def function(signature):
    start = source.index(signature); end = source.index('{', start) + 1; depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[start:end]
constants = source[source.index('#define SR_FIFOE'):source.index('/* codec registers */')]
registers = source[source.index('/* bus masters */'):source.index('/* PCM out pacing:')]
pacing = source[source.index('#define PO_PERIOD_NS'):source.index('typedef struct BD {')]
types = source[source.index('typedef struct BD {'):source.index('#define MCPX_ACI_DEVICE')]
production = '\n'.join(function(name) for name in (
    'static void po_open(', 'static void po_set_active(', 'static void po_earn(', 'static uint32_t po_take(',
    'static void so_set_active(', 'static void so_earn(', 'static uint32_t so_take(',
    'static void fetch_bd(', 'static void update_sr(', 'static void voice_set_active(',
    'static void reset_bm_regs(', 'static void reset_bus_masters(', 'static void transfer_playback(',
    'static void transfer_po(', 'static void transfer_so(', 'static void po_tick(', 'static void so_tick(',
    'static void nabm_writeb(', 'static void nabm_writew(', 'static void nabm_writel(', 'static int mcpx_aci_post_load('))
assert '.post_load = mcpx_aci_post_load,' in source
schema = source[source.index('static const VMStateDescription vmstate_mcpx_aci'):source.index('static void mcpx_aci_class_init(')]
assert '.version_id = 3,' in schema and '.minimum_version_id = 1,' in schema
for field in ('so_active', 'so_credit', 'so_last_ns', 'so_rem', 'so_timer'):
    assert f'({field}, MCPXACIState, 3)' in source, field
realize = function('static void mcpx_aci_realize(')
assert 'lr_dsp_register_guarded_timer(so_tick);' in realize
assert 'timer_new_ns(QEMU_CLOCK_VIRTUAL, so_tick, d)' in realize
assert 'timer_free(d->so_timer);' in function('static void mcpx_aci_exit(')
code = Path(__file__).with_suffix('.c').read_text().replace('/* CONSTANTS */', constants).replace('/* REGISTERS */', registers).replace('/* PACING */', pacing).replace('/* TYPES */', types).replace('/* PRODUCTION */', production)
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
flags = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror'] + shlex.split(os.environ.get('ACI_PLAYBACK_CFLAGS', '-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
runner = shlex.split(os.environ.get('ACI_PLAYBACK_RUNNER', ''))
mutations = {
    'missing-spdif-clock': ('so_set_active(s, on);', '(void)on;'),
    'shared-pcm-credit': ('index == SO_INDEX ? so_take(s, temp) : po_take(s, temp)', 'po_take(s, temp)'),
    'legacy-clock': ('if (version < 3) {', 'if (version < 0) {'),
    'odd-descriptor-stall': ('got -= got % SO_DMA_UNIT_BYTES;', 'got -= got % SO_FRAME_BYTES;'),
}
with tempfile.TemporaryDirectory(prefix='aci-playback-') as temporary:
    path = Path(temporary)
    for name in ('production', *mutations):
        test = code
        if name in mutations:
            old, new = mutations[name]; assert test.count(old) == 1; test = test.replace(old, new, 1)
        c = path / 'test.c'; exe = path / 'test.exe'; c.write_text(test)
        subprocess.run(compiler + flags + [str(c), '-o', str(exe)], check=True)
        result = subprocess.run(runner + [str(exe)], capture_output=True, timeout=30)
        if name == 'production':
            sys.stdout.buffer.write(result.stdout)
            if result.returncode: sys.stderr.buffer.write(result.stderr)
            result.check_returncode()
        else:
            assert result.returncode and b'ACI playback:' in result.stderr, (name, result.stderr)
            print('Negative control rejected:', name)

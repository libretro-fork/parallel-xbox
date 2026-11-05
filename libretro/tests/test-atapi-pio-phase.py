# SPDX-License-Identifier: GPL-2.0-or-later
"""Delayed-provider byte oracles for the production ATAPI PIO phase path."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
root = Path(sys.argv[1]).resolve(); cc = sys.argv[2] if len(sys.argv)>2 else 'cc'
s = (root/'hw/ide/atapi.c').read_text()
def between(a,b): return s[s.index(a):s.index(b,s.index(a))]
code = (between('static void lba_to_msf(', 'static inline int media_present') +
        between('static void cd_data_to_raw(', 'void ide_atapi_cmd_ok(') +
        between('static uint16_t atapi_byte_count_limit(', "/* send a reply of 'size'") +
        between('static void ide_atapi_cmd_read_pio(', 'static void ide_atapi_cmd_check_status('))
core = (root/'hw/ide/core.c').read_text()
a = core.index('static int ide_drive_pio_post_load(')
b = core.index('static bool ide_drive_pio_state_needed(', a)
idx = core.index('static int transfer_end_table_idx(')
idx_end = core.index('static int ide_drive_post_load(',idx)
code += core[idx:idx_end] + core[a:b]
state=core[core.index('static const VMStateDescription vmstate_ide_drive_pio_state'):]
state=state[:state.index('const VMStateDescription vmstate_ide_drive =')]
for field in ['lba','cd_sector_size','io_buffer_index','io_buffer_size']:
    assert 'VMSTATE_INT32_V('+field+', IDEState, 2)' in state, field
assert "VMSTATE_BOOL_V(atapi_pio_pending, IDEState, 3)" in state
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',code)
with tempfile.TemporaryDirectory(prefix='atapi-pio-') as tmp:
    p=Path(tmp); (p/'test.c').write_text(code)
    binary=os.environ.get('ATAPI_PIO_BINARY',str(p/'test'))
    flags=shlex.split(os.environ.get('ATAPI_PIO_CFLAGS',
                    '-static' if 'mingw' in cc else '-fsanitize=address,undefined'))
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,
                    str(p/'test.c'),'-o',binary],check=True)
    if not os.environ.get('ATAPI_PIO_SKIP_RUN'):
        subprocess.run([binary],check=True,timeout=60)

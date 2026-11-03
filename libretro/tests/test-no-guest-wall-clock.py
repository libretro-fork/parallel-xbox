# SPDX-License-Identifier: GPL-2.0-or-later
"""Prevent host-clock reads from returning to guest execution and rendering."""
from pathlib import Path
import re, sys
root=Path(sys.argv[1]).resolve()
files=list((root/'hw/xbox').rglob('*.c'))
files += [root/name for name in ['system/rtc.c','system/cpu-timers.c','accel/tcg/cpu-exec.c','accel/tcg/cputlb.c','accel/tcg/tcg-accel-ops-icount.c','ui/libretro.c','ui/libretro-state.c']]
pattern=re.compile(r'\b(?:get_clock|get_clock_realtime|cpu_get_host_ticks|g_get_monotonic_time|g_get_real_time|cpu_features_get_time_usec|QueryPerformanceCounter|GetSystemTimeAsFileTime|clock_gettime|gettimeofday)\s*\(|\btime\s*\(\s*(?:NULL|0)\s*\)|qemu_clock_get_\w+\s*\(\s*QEMU_CLOCK_(?:HOST|REALTIME|VIRTUAL_RT)')
for path in files:
    text=path.read_text(encoding='utf-8',errors='surrogateescape')
    assert not pattern.search(text), str(path.relative_to(root))
assert 'g_get_real_time' not in (root/'libretro/glib/gcore.c').read_text()
print(f'Guest wall-clock audit: {len(files)} execution/device/renderer files passed; host timeout infrastructure excluded')

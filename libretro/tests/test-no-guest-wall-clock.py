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
physical=re.compile(r'\b(?:QueryPerformanceCounter|QueryPerformanceFrequency|GetSystemTimeAsFileTime|clock_gettime|gettimeofday|cpu_features_get_time_usec)\s*\(')
for name in ['libretro/glib/gcore.c','util/qemu-timer-common.c','include/qemu/timer.h']:
 assert not physical.search((root/name).read_text()),name
flags=(root/'Makefile.common').read_text()
assert '-DRETRO_EVENTCOUNT_FIXED_SPIN=256' in flags
assert '-DRTHREADS_SCOND_FIXED_SPIN=256' in flags
assert 'retro_eventcount_commit_wait_timeout' not in (root/'hw/xbox/nv2a/pfifo.c').read_text()
print(f'Clock audit: {len(files)} guest files and service-clock providers contain no host timestamp reads')

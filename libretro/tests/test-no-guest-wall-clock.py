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
assert 'g_usleep(' not in (root/'chardev/char.c').read_text()
print(f'Clock audit: {len(files)} guest files and service-clock providers contain no host timestamp reads')

# Audit every explicitly listed platform variant, not just Xbox devices.
# Generic support libraries retain unused clock/timeout APIs; their untimed
# core paths are tested with trapped clock reads by test-clockless-thread-waits.
manifest=set(re.findall(r'\$\(CORE_DIR\)/([\w./-]+\.c)\b',flags))
assert len(manifest)>300, 'static Makefile source inventory unexpectedly empty'
providers={'libretro-common/time/rtime.c','libretro-common/features/features_cpu.c','libretro-common/rthreads/rthreads.c',
           'libretro-common/rthreads/retro_eventcount.c','libretro-common/file/file_path.c'}
physical_all=re.compile(r'\b(?:QueryPerformanceCounter|QueryPerformanceFrequency|GetSystemTimeAsFileTime|GetSystemTimePreciseAsFileTime|GetTickCount64?|clock_gettime|ra_clock_gettime|gettimeofday|timespec_get|mach_absolute_time|cpu_features_get_time_usec|time)\s*\(')
# These helpers read host time or schedule an explicit host duration. No
# core caller may reach them through the shared support library interface.
helpers=re.compile(r'\b(?:cpu_features_get_time_usec|retro_sleep_until_us|fill_dated_filename|fill_str_dated_filename|retro_sleep_us|g_usleep)\s*\(')
for name in sorted(manifest):
 path=root/name
 assert path.is_file(), name
 if name in providers:continue
 text=path.read_text(encoding='utf-8',errors='surrogateescape')
 text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
 assert not physical_all.search(text), 'host timestamp in built source: '+name
 assert not helpers.search(text), 'host-time support helper in built source: '+name
assert 'g_usleep' not in (root/'libretro/glib/glib.h').read_text()
print(f'Static Makefile audit: {len(manifest)} source files checked across platform variants; {len(providers)} generic API providers explicitly isolated')

assert 'main_loop_wait_timeout' not in (root/'util/main-loop.c').read_text()
assert 'retro_eventcount_commit_wait_timeout' not in (root/'util/main-loop.c').read_text()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production shader cache publication and immutable disk-save jobs."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else "cc")
source = (root / "hw/xbox/nv2a/pgraph/gl/shaders.c").read_text()
starts = ["typedef struct ShaderDiskResult {", "static void shader_load_from_disk(", "static void shader_adopt_disk_results(",
          "static void shader_write_lru_list_entry_to_disk(",
          "void pgraph_gl_shader_write_cache_reload_list(",
          "static void shader_write_to_disk(", "static bool shader_start_disk_save("]
mode_filter = 'static void shader_reload_lru_start(' in source
if os.environ.get('SHADER_CACHE_REQUIRE_MODE_FILTER'):
    assert mode_filter, 'Required shader cache mode filter is missing'
if mode_filter:
    starts += ['static void shader_reload_lru_from_disk(', 'static void shader_reload_lru_start(']
    begin = source.index('void pgraph_gl_init_shaders(')
    end = source.index('{', begin)+1
    depth = 1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}')
        end += 1
    init = source[begin:end]
    assert 'shader_reload_lru_start(pg);' in init
    assert 'sthread_create(shader_reload_lru_from_disk' not in init
    assert init.index('retro_mpsc_init(') < init.index('shader_reload_lru_start(pg);')
pieces = []
for start in starts:
    begin = source.index(start)
    ending = "\n} ShaderDiskResult;" if start.startswith("typedef") else "\n}"
    end = source.index(ending, begin) + len(ending)
    pieces.append(source[begin:end])
with tempfile.TemporaryDirectory(prefix="shader-cache-handoff-") as temporary:
    directory = Path(temporary)
    production = "\n\n".join(pieces)
    fault = os.environ.get('SHADER_CACHE_MODE_FAULT', '')
    if fault:
        assert mode_filter, 'Mode fault requires the production mode selector'
        faults = {
            'eager-sso': ('if (!g_config.perf.cache_shaders || gl_sso_enabled())',
                          'if (!g_config.perf.cache_shaders)'),
            'overwrite-sso': ('if (!gl_sso_enabled()) {', 'if (true) {'),
            'drop-maintenance': ('    seed_write_files(r);',
                                 '    if (gl_sso_enabled()) return;\n    seed_write_files(r);'),
        }
        before, after = faults[fault]
        assert production.count(before)==1, fault
        production = production.replace(before, after)
    (directory / "shader-cache-production.h").write_text(production)
    cursor=source[source.index('typedef struct FileCursor'):source.index('static bool shader_read_cache_version')]
    (directory / "shader-cache-cursor.h").write_text(cursor)
    windows = os.name == "nt" or "mingw" in compiler[0]
    binary = Path(os.environ["SHADER_CACHE_BINARY"]) if "SHADER_CACHE_BINARY" in os.environ else (
        directory / ("test.exe" if windows else "test"))
    command = compiler + ["-O1", "-g", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                         "-Wno-unused-parameter", "-DHAVE_THREADS",
                         "-I" + str(root), "-I" + str(root / "include"),
                         "-I" + str(root / "libretro-common/include"), "-I" + str(directory)]
    command += shlex.split(os.environ.get("SHADER_CACHE_CFLAGS", ""))
    if mode_filter:
        command += ['-DHAVE_SHADER_CACHE_MODE_FILTER']
    command += [str(root / path) for path in ["libretro/tests/test-shader-cache-handoff.c",
                                            "libretro-common/rthreads/rthreads.c", "libretro-common/rthreads/retro_eventcount.c"]]
    if windows:
        command += ["-Wno-cast-function-type"]
        command += [str(root / path) for path in [
            "libretro-common/compat/compat_strl.c","libretro-common/features/features_cpu.c", "libretro-common/time/rtime.c"]]
    command += ["-pthread", "-o", str(binary)]
    env = dict(os.environ)
    if Path(compiler[0]).is_absolute():
        env['PATH'] = str(Path(compiler[0]).parent) + os.pathsep + env.get('PATH', '')
    subprocess.run(command, check=True, env=env)
    if not os.environ.get("SHADER_CACHE_SKIP_RUN"):
        subprocess.run([str(binary), str(directory)], check=True, timeout=60, env=env)

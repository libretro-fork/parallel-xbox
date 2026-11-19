#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the actual pre-draw resource selection with creation tripwires."""
from pathlib import Path
import argparse, os, subprocess, tempfile
p = argparse.ArgumentParser()
p.add_argument('repo', type=Path)
p.add_argument('cc', nargs='?', default='cc')
a = p.parse_args()
source = (a.repo / 'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()

def function(name):
    start = source.index('static ', source.index('static bool clear_needs_color_draw') if name == 'clear_needs_color_draw' else source.index('static void begin_pre_draw'))
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

clear = function('clear_needs_color_draw')
body = function('begin_pre_draw')
template = Path(__file__).with_suffix('.c').read_text()
mutants = [('positive', body)]
for label, old, new in [
    ('dynamic-framebuffer-create', 'if (!r->shader_objects.draw) create_frame_buffer(pg);', 'create_frame_buffer(pg);'),
    ('dynamic-null-repair', 'if (!r->shader_objects.draw && r->framebuffer == VK_NULL_HANDLE)', 'if (r->framebuffer == VK_NULL_HANDLE)'),
    ('stale-classic-repair', 'if (!r->shader_objects.draw && r->framebuffer == VK_NULL_HANDLE)', 'if (false)'),
    ('dirty-scope-not-consumed', 'r->framebuffer_dirty = false;', 'r->framebuffer_dirty = true;'),
    ('clear-keeps-dynamic', 'if (pg->clearing) r->shader_objects.draw = false;', 'if (false) r->shader_objects.draw = false;'),
]:
    assert body.count(old) == 1, label
    mutants.append((label, body.replace(old, new)))
env = dict(os.environ)
env['PATH'] = str(Path(a.cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='vk-dynamic-framebuffer-') as name:
    out = Path(name)
    for label, code in mutants:
        c = out / (label + '.c')
        exe = out / (label + ('.exe' if os.name == 'nt' else ''))
        c.write_text(template.replace('/* PRODUCTION CLEAR CLASSIFIER */', clear).replace('/* PRODUCTION PRE-DRAW */', code))
        subprocess.run([a.cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True, env=env)
        result = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
        if label == 'positive':
            assert result.returncode == 0, result.stdout + result.stderr
            print(result.stdout, end='')
        else:
            assert result.returncode != 0 and 'assert' in result.stderr.lower(), (label, result.returncode, result.stdout, result.stderr)
            print('Rejected:', label)
assert (a.repo / 'hw/xbox/nv2a/pgraph/vk/draw.c').read_text() == source
print('All five allocation/lifetime faults rejected PASS')

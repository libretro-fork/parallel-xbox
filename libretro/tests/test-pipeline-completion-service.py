#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Full-PSO completion must not depend on GPL, dynamic state or an attachment."""
from pathlib import Path
import os, re, subprocess, sys, tempfile

root = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
source = (root / 'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
function = re.search(r'^void pgraph_vk_libpump_step\(.*?^\}', source, re.M | re.S).group()
assert function.count('    unsigned n;') == 1
prefix = function.split('    unsigned n;')[0]
code = r'''
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {
    bool device, pipeline_library, dyn_cull_front, dyn_blend, dyn_vertex_input, color_binding, zeta_binding;
} PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; } PGRAPHState;
static bool pipe_started;
static unsigned pump_d,pump_w_local;
static struct { struct { bool shader_seeding; } perf; } g_config;
static unsigned drains, pumped;
static void pump_drain(PGRAPHState *pg) { (void)pg; drains++; }
/* PRODUCTION */
    pumped++;
}
int main(void) {
    for (unsigned bits = 0; bits < 2048; bits++) {
        PGRAPHVkState r = {.device=bits&2, .pipeline_library=bits&4,
            .dyn_cull_front=bits&8, .dyn_blend=bits&16,
            .dyn_vertex_input=bits&32, .color_binding=bits&64,.zeta_binding=bits&1024};
        PGRAPHState pg = {.vk_renderer_state=(bits&1) ? &r : NULL};
        pipe_started=bits&128; drains=pumped=0;
        g_config.perf.shader_seeding=bits&256;
        pump_d=0;pump_w_local=!!(bits&512);
        pgraph_vk_libpump_step(&pg);
        bool alive=(bits&1) && r.device;
        unsigned expected_drain=alive && pipe_started && pump_w_local;
        unsigned expected_pump=alive && g_config.perf.shader_seeding && r.dyn_cull_front &&
            r.dyn_blend && r.dyn_vertex_input && (r.color_binding||r.zeta_binding);
        if (drains != expected_drain || pumped != expected_pump) {
            fprintf(stderr,"FAIL completion bits=%u drains=%u expected=%u\n",bits,drains,expected_drain);
            return 1;
        }
    }
    puts("Pipeline completion: 2048 renderer/device/GPL/dynamic/attachment/worker/option/idle cases PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='pipeline-completion-') as folder:
    folder = Path(folder)
    fixture = folder / 'test.c'
    executable = folder / ('test.exe' if os.name == 'nt' else 'test')
    command = [cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
               str(fixture), '-o', str(executable)]
    fixture.write_text(code.replace('/* PRODUCTION */', prefix))
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True, timeout=30)
    # Reintroduce the old misplaced GPL guard. The no-GPL case must catch it.
    old = 'if (pipe_started && pump_d != pump_w_local) {'
    assert prefix.count(old) == 1
    mutant = prefix.replace(old, 'if (pipe_started && pump_d != pump_w_local && r->pipeline_library) {')
    fixture.write_text(code.replace('/* PRODUCTION */', mutant))
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable)], capture_output=True, timeout=30)
    assert result.returncode == 1 and b'FAIL completion' in result.stderr
    print('Required negative control rejected: GPL-only completion servicing')

    for name,mutant in [('GPL-only seed preparation',prefix.replace('!g_config.perf.shader_seeding ||','!g_config.perf.shader_seeding || !r->pipeline_library ||',1)),('depth-only preparation skipped',prefix.replace('(!r->color_binding && !r->zeta_binding)','!r->color_binding',1))]:
        assert mutant!=prefix,name
        fixture.write_text(code.replace('/* PRODUCTION */',mutant));subprocess.run(command,check=True)
        bad=subprocess.run([str(executable)],capture_output=True,timeout=30)
        assert bad.returncode==1 and b'FAIL completion' in bad.stderr,name
        print('Required negative control rejected:',name)

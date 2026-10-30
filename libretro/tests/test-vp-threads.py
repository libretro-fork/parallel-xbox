# SPDX-License-Identifier: GPL-2.0-or-later
"""The voice processor's frame on four threads against the same on one.
Usage: python test-vp-threads.py CHECKOUT [CC] [SCENARIOS]
vp.c's own functions (decode to mix, the unit schedule, claims and workers)
around test-voice-tail.c's fixture and libretro-common's threads.
Set VP_THREADS_CFLAGS, e.g. -fsanitize=thread.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
scenarios = sys.argv[3] if len(sys.argv) > 3 else '400'
here = Path(__file__).resolve().parent
source = (repo/'hw/xbox/mcpx/apu/vp/vp.c').read_text(encoding='utf-8')
header = (repo/'hw/xbox/mcpx/apu/vp/vp.h').read_text(encoding='utf-8')

def function(name):
    match = re.search(r'^(?:static\s+)?(?:inline\s+)?(?:void|int|int32_t|uint32_t|bool|hwaddr|VoicePitchRamp)\s*\n?\s*'
                      + name + r'\([^;]*?\)\s*\{', source, re.M)
    assert match, name
    start = source.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'

types = header[header.index('typedef struct MCPXAPUVPSSLData'):header.index('typedef struct {\n    MemoryRegion mmio;')]
pitch = source[source.index('typedef struct VoicePitchRamp'):source.index('/* Keep exact log-pitch')]
table = source[source.index('static const uint16_t attenuation_nodes'):source.index('static uint32_t attenuation_gain')]
fixture = (here/'test-voice-tail.c').read_text(encoding='utf-8').split('/* INSERT PRODUCTION FUNCTIONS */')[0]
fixture = fixture.replace('#include "hw/xbox/mcpx/apu/sample.h"', '''#include "hw/xbox/mcpx/apu/sample.h"
#include "hw/xbox/mcpx/apu/monitor.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include "qemu/processor.h"
#define MAX_VOICE_WORKERS 16
static void rcu_register_thread(void){}
static void rcu_unregister_thread(void){}
static unsigned g_get_num_processors(void){return 8;}
static void *g_malloc0_n(size_t n,size_t s){return calloc(n,s);}
static void g_free(void*p){free(p);}''')
fixture = fixture.replace('/* TYPES */', types + pitch)
fixture = fixture.replace('static struct {struct {bool hrtf;}audio;}g_config;',
                          'static struct {struct {bool hrtf;struct {int num_workers;}vp;}audio;}g_config;')
fixture = fixture.replace('        uint64_t voice_locked[4];',
                          '        uint64_t voice_locked[4];\n        VoiceWorkDispatch voice_work_dispatch;')
fixture = fixture.replace('    int set_irq;', '    retro_atomic_int_t set_irq;')
fixture = fixture.replace('#define retro_atomic_fetch_or_int(p,v) (*(p)|=(v))\n#define retro_atomic_store_relaxed_int(p,v) (*(p)=(v))\n', '')
# the fixture's multipass stubs give way to vp.c's own
fixture = re.sub(r'static int peek_ahead_multipass_bin\(.*?\n', '', fixture)
fixture = re.sub(r'static bool allow_multipass;\n', '', fixture)
fixture = re.sub(r'static void get_multipass_samples\(.*?\n\{.*?\}\n', '', fixture, flags=re.S)
functions = ['set_notify_status', 'voice_get_mask', 'voice_set_mask', 'voice_off',
             'voice_resample_reset', 'voice_reset_filters', 'filter_coefficient', 'attenuation_gain',
             'get_data_ptr', 'envelope_level', 'voice_step_envelope', 'voice_step_lfos',
             'voice_get_samples', 'voice_resample_fetch', 'voice_pitch_ramp',
             'voice_pitch_step', 'voice_resample', 'peek_ahead_multipass_bin',
             'get_multipass_samples', 'voice_process', 'hrir_tap',
             'get_voice_bin_src_dst', 'voice_work_claim', 'voice_work_unit', 'voice_worker_thread',
             'voice_work_enqueue', 'voice_work_schedule', 'voice_work_dispatch', 'voice_work_init',
             'voice_work_finalize']
driver = (here/'test-vp-threads.c').read_text(encoding='utf-8')
generated = fixture + table + ''.join(function(n) for n in functions) + \
    driver.replace('/* VOICE FRAME */', function('mcpx_apu_vp_frame'))
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
lrc = repo/'libretro-common'
with tempfile.TemporaryDirectory(prefix='vp-threads-', dir=repo.parent) as directory:
    tmp = Path(directory)
    c = tmp/'test.c'
    c.write_text(generated, encoding='utf-8')
    exe = tmp/('test.exe' if os.name == 'nt' else 'test')
    args = [cc, '-O2', '-g', '-fwrapv', '-fno-strict-aliasing', '-std=gnu11', '-w', '-pthread',
            '-I' + str(repo), '-I' + str(repo/'include'), '-I' + str(lrc/'include'), str(c),
            str(lrc/'rthreads/rthreads.c'), str(lrc/'rthreads/retro_eventcount.c'), '-o', str(exe)]
    args += shlex.split(os.environ.get('VP_THREADS_CFLAGS', ''))
    if os.name != 'nt':
        args += ['-lm']
    subprocess.run(args, env=env, check=True)
    subprocess.run([str(exe), scenarios], env=env, check=True, timeout=300)

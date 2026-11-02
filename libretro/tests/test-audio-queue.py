# SPDX-License-Identifier: GPL-2.0-or-later
"""Production mirrored audio queue: cadence, EP bursts, reset and state restore."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
source=(repo/'hw/xbox/mcpx/apu/monitor.c').read_text()
source=re.sub(r'^#include "(?:apu_int.h|dsp/gp_ep.h)"\n','',source,flags=re.M)
source=source.replace('#include "monitor.h"','#include "hw/xbox/mcpx/apu/monitor.h"')
apu=(repo/'hw/xbox/mcpx/apu/apu.c').read_text()
a=apu.index('static int mcpx_apu_output_post_load(');b=apu.index('\n}',a)+2
fixture=Path(__file__).with_suffix('.c').read_text()
fixture=fixture.replace('/* MONITOR */',source).replace('/* POST LOAD */',apu[a:b])
with tempfile.TemporaryDirectory(prefix='audio-queue-') as directory:
 tmp=Path(directory);c=tmp/'test.c';c.write_text(fixture)
 exe=Path(os.environ.get('AUDIO_QUEUE_BINARY',str(tmp/'test')))
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
  '-I'+str(repo),'-I'+str(repo/'libretro-common/include'),str(c),
  str(repo/'libretro-common/queues/retro_spsc.c'),'-lm',
  *shlex.split(os.environ.get('AUDIO_QUEUE_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error')),
  '-o',str(exe)],check=True)
 if not os.environ.get('AUDIO_QUEUE_SKIP_RUN'):subprocess.run([str(exe)],check=True,timeout=60)

# SPDX-License-Identifier: GPL-2.0-or-later
"""Real decoder/fetch/resampler/voice-frame regression, with bounded guest RAM.
Usage: python test-voice-tail.py CHECKOUT [GCC]
Optional TAIL_TEST_CFLAGS for sanitizers; functions are read from CHECKOUT.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile

repo=Path(sys.argv[1]).resolve()
cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
source=(repo/'hw/xbox/mcpx/apu/vp/vp.c').read_text(encoding='utf-8')
header=(repo/'hw/xbox/mcpx/apu/vp/vp.h').read_text(encoding='utf-8')
def function(name):
    match=re.search(r'^(?:static\s+)?(?:inline\s+)?(?:void|float|int|uint32_t|hwaddr|VoicePitchRamp)\s+'+name+r'\([^;]*?\)\s*\{',source,re.M)
    assert match,name
    start=source.index('{',match.start())
    depth,end=1,start+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]+'\n'
types=header[header.index('typedef struct MCPXAPUVPSSLData'):header.index('typedef struct VoiceWorkItem')]
pitch=source[source.index('typedef struct VoicePitchRamp'):source.index('/* Keep exact log-pitch')]
table=source[source.index('static const uint16_t attenuation_nodes'):source.index('static uint32_t attenuation_gain')]
fixture=Path(__file__).with_suffix('.c').read_text(encoding='utf-8')
before,after=fixture.split('/* INSERT PRODUCTION FUNCTIONS */')
before=before.replace('/* TYPES */',types+pitch)
functions=['set_notify_status','voice_get_mask','voice_set_mask','voice_off',
    'voice_resample_reset','voice_reset_filters','filter_coefficient','attenuation_gain',
    'get_data_ptr','envelope_level','voice_step_envelope','voice_step_lfos',
    'voice_get_samples','voice_resample_fetch','lagrange_init','voice_pitch_ramp',
    'voice_pitch_step','voice_resample','voice_process','mcpx_apu_vp_reset']
generated=before+table+'static float lagrange_rows[257][8];\n'+''.join(function(n) for n in functions)
generated+=after.replace('/* VOICE FRAME */',function('mcpx_apu_vp_frame'))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='voice-tail-',dir=repo.parent) as directory:
    tmp=Path(directory);c=tmp/'test.c';c.write_text(generated,encoding='utf-8')
    exe=tmp/('test.exe' if os.name=='nt' else 'test')
    args=[cc,'-O2','-g','-fwrapv','-fno-strict-aliasing','-std=gnu11','-Wall','-Wextra','-Werror',
          '-Wno-unused-parameter','-Wno-unused-function','-Wno-sign-compare','-Wno-error=maybe-uninitialized','-I'+str(repo),str(c),'-o',str(exe)]
    args+=shlex.split(os.environ.get('TAIL_TEST_CFLAGS',''))
    if os.name!='nt':args+=['-lm']
    subprocess.run(args,env=env,check=True)
    subprocess.run([str(exe),*sys.argv[3:]],env=env,check=True,timeout=60)

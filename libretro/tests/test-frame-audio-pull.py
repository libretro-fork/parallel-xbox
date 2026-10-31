# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production per-run audio pull and its saved cadence phase."""
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / 'ui/libretro.c').read_text(errors='surrogateescape')
start = source.index('static int lr_audio_frames_this_run(void)')
end = source.index('/* The picture of the frame just run,', start)
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static int32_t lr_audio_rem;
static int16_t samples[802*2];
static size_t available, consumed, callbacks, total, expected;
static int16_t sequence(size_t i) { return (int16_t)(i%30001); }
static size_t sink(const int16_t *p,size_t n) {
    assert(n==expected);callbacks++;total+=n;
    for(size_t i=0;i<n*2;i++) assert(p[i]==sequence(consumed*2+i));
    return n;
}
static size_t (*audio_batch_cb)(const int16_t *,size_t)=sink;
static size_t libretro_audio_read_begin(const int16_t **p) { *p=samples;return available; }
static void libretro_audio_read_end(size_t n) { assert(n<=available);consumed+=n;available-=n; }
/* PRODUCTION */
int main(void) {
    for(int phase=0;phase<5;phase++) {
        lr_audio_rem=phase;consumed=callbacks=total=0;
        for(size_t frame=0;frame<10000;frame++) {
            expected=((frame+1)*4004+phase)/5-(frame*4004+phase)/5;
            available=802;
            for(size_t i=0;i<available*2;i++) samples[i]=sequence(consumed*2+i);
            lr_present_audio(true);
            assert(callbacks==frame+1 && consumed==total);
        }
        assert(total==8008000 && lr_audio_rem==phase);
    }
    puts("audio pull: 50000 runs, exact 800/801 cadence and consecutive stereo samples passed");
}
'''.replace('/* PRODUCTION */', source[start:end])
with tempfile.TemporaryDirectory(prefix='frame-audio-pull-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-fsanitize-undefined-trap-on-error', str(tmp / 'test.c'),
                    '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True, timeout=15)

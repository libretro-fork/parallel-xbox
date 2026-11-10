# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the real ARM64 jump emitter and reset paths under qemu-user."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
def function(path,key):
    s=(r/path).read_text(); a=s.index(key); b=s.index('{',a)+1; depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}'); b+=1
    return s[a:b]
body='\n'.join(function(path,key) for path,key in [
    ('accel/tcg/cpu-exec.c','void tb_set_jmp_target('),
    ('accel/tcg/tb-maint.c','void tb_reset_jump('),
    ('accel/tcg/translate-all.c','static inline void tb_init_jump(')])
source=(r/'accel/tcg/translate-all.c').read_text()
assert 'tb_init_jump(tb, 0, recycled);' in source and 'tb_init_jump(tb, 1, recycled);' in source
with tempfile.TemporaryDirectory(prefix='arm64-fresh-jump-') as d:
    p=Path(d); (p/'reset.h').write_text(body)
    flags=['-O2','-std=gnu11','-fno-strict-aliasing','-fwrapv','-ffunction-sections',
           '-fdata-sections','-Wl,--gc-sections','-D_GNU_SOURCE','-DHAVE_THREAD_STORAGE',
           '-DCONFIG_SOFTMMU','-DCOMPILING_SYSTEM_VS_USER','-I'+str(p)]
    for path in ['libretro/gen','.','libretro/glib','libretro-common/include','include',
                 'host/include/aarch64','host/include/generic','tcg','tcg/aarch64']:
        flags+=['-I'+str(r/path)]
    flags+=shlex.split(os.environ.get('ARM64_JUMP_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
    cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'aarch64-linux-gnu-gcc')
    subprocess.run(cc+flags+[str(Path(__file__).with_suffix('.c')),'-o',str(p/'test')],check=True)
    runner=shlex.split(os.environ.get('ARM64_JUMP_RUNNER','qemu-aarch64 -L /usr/aarch64-linux-gnu'))
    subprocess.run(runner+[str(p/'test')],check=True,timeout=60)
    backend=(r/'tcg/aarch64/tcg-target.c.inc').read_text()
    core=(r/'tcg/tcg.c').read_text()
    assert 'flush_idcache_range((uintptr_t)tcg_splitwx_to_rx(s->code_buf)' in core
    (p/'tcg').mkdir(); (p/'tcg/tcg.c').write_text(core)
    controls=[('fresh branch left uninitialized',body,backend.replace(
        'tcg_out32(s, deposit32(I3206_B, 0, 26, 2));','tcg_out32(s, I3206_B);')),
        ('recycled reset omitted',body.replace('if (!recycled)', 'if (true)'),backend),
        ('fresh jump patched redundantly',body.replace('if (!recycled)', 'if (false)'),backend)]
    for name,reset,emitter in controls:
        assert reset!=body or emitter!=backend
        (p/'reset.h').write_text(reset); (p/'tcg-target.c.inc').write_text(emitter)
        subprocess.run(cc+flags+[str(Path(__file__).with_suffix('.c')),'-o',str(p/'test')],check=True)
        result=subprocess.run(runner+[str(p/'test')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
        assert result.returncode,name
        assert b'assert' in result.stdout.lower(),result.stdout
        print('Required negative control rejected:',name,flush=True)
    # Other backends retain their previous reset path for every block.
    fallback='''#include <assert.h>
#include <stdbool.h>
typedef struct {unsigned resets;} TranslationBlock;
static void tb_reset_jump(TranslationBlock *tb,int n) {assert(n==0 || n==1);tb->resets++;}
'''+function('accel/tcg/translate-all.c','static inline void tb_init_jump(')+'''
int main(void) {TranslationBlock tb={0};
for(unsigned slot=0;slot<2;slot++) for(unsigned recycled=0;recycled<2;recycled++) tb_init_jump(&tb,slot,recycled);
assert(tb.resets==4);}
'''
    (p/'fallback.c').write_text(fallback)
    subprocess.run(cc+['-O2',str(p/'fallback.c'),'-o',str(p/'fallback')],check=True)
    subprocess.run(runner+[str(p/'fallback')],check=True,timeout=30)
    print('Non-reset-form backend fallback PASS',flush=True)

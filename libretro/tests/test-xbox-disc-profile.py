# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify load-time disc identity and cached ATAPI profiles with production functions."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
def function(s,name):
    m=re.search(r'^(?:static )?(?:inline )?(?:bool|void|int|uint64_t|const (?:uint8_t|void) \*) *'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
    a=s.index('{',m.start());b=a+1;depth=1
    while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[m.start():b]
disk=(repo/'block/libretro-disk.c').read_text();vl=(repo/'system/vl.c').read_text();ui=(repo/'ui/libretro-settings-stub.c').read_text()
atapi=Path(os.environ.get('XBOX_DISC_ATAPI_SOURCE',repo/'hw/ide/atapi.c')).read_text()
types=re.search(r'typedef enum \{\s*LR_MEDIUM_NONE,.*?\} LRMedium;',disk,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types)
code=code.replace('/* PROBE */',function(vl,'xbox_read_at')+'\n'+function(vl,'xbox_game_partition_offset')+'\n'+function(ui,'xemu_media_is_xbox_disc'))
code=code.replace('/* BACKEND */','\n'.join(function(disk,n) for n in ['lr_disk_set_xbox_disc','lr_disk_is_xbox_disc','lr_medium_close','lr_disk_open','lr_disk_open_memory','lr_disk_has_tray','lr_disk_change']))
code=code.replace('/* ATAPI */','\n'.join(function(atapi,n) for n in ['media_present','media_is_dvd','media_is_cd']))
# Both startup and swap must apply classification only after a successful open.
assert 'lr_disk_set_xbox_disc(cd, xbox_disc);' in vl
assert 'if (lr_disk_change(blk, path, offset, errp)) {\n        lr_disk_set_xbox_disc(blk, xbox_disc);' in vl
with tempfile.TemporaryDirectory(prefix='xbox-disc-') as tmp:
    p=Path(tmp);c=p/'test.c';c.write_text(code);exe=Path(os.environ.get('XBOX_DISC_BINARY',p/'test'))
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(repo/'include'),*shlex.split(os.environ.get('XBOX_DISC_CFLAGS','-fsanitize=address,undefined')),str(c),'-o',str(exe)],check=True)
    if not os.environ.get('XBOX_DISC_SKIP_RUN'):subprocess.run([str(exe)],check=True,timeout=30)

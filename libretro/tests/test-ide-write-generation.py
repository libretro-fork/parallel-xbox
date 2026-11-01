# SPDX-License-Identifier: GPL-2.0-or-later
"""Preserve the one live block statistic: successful IDE writes used by save states."""
from pathlib import Path
import argparse,os,re,shlex,shutil,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');a=p.parse_args();r=a.checkout.resolve()
def function(path,name):
 s=(r/path).read_text(encoding='utf-8');m=re.search(r'^(?:static )?(?:void|uint64_t) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name;lo=m.start();hi=s.index('{',lo)+1;depth=1
 while depth:
  depth+=(s[hi]=='{')-(s[hi]=='}');hi+=1
 return s[lo:hi]
parts=[function('block/libretro-disk.c',n) for n in ['blk_note_guest_write','blk_get_guest_write_generation']]+[function('ui/libretro-state.c','lr_state_hdd_writes'),function('hw/ide/core.c','ide_sector_write_cb')]
s=(r/'hw/ide/core.c').read_text(encoding='utf-8');dma=s[s.index('static void ide_dma_cb(void *opaque, int ret)\n{'):];dma=dma[:dma.index('\nstatic void ide_sector_start_dma')];assert 'if (s->dma_cmd == IDE_DMA_WRITE) {\n        blk_note_guest_write(s->blk);' in dma
for path in ['hw/ide/core.c','hw/ide/atapi.c','hw/block/block.c','block/libretro-disk.c','ui/libretro-state.c']:
 text=(r/path).read_text(encoding='utf-8');assert not re.search(r'block_acct|blk_get_stats|BlockAcct',text),path
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='ide-write-generation-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' or 'mingw' in a.cc else 'test')
 flags=shlex.split(os.environ.get('IDE_WRITE_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' else '-g'))
 env=os.environ.copy();env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r/'libretro-common/include'),*flags,str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 if os.environ.get('IDE_WRITE_BINARY'):shutil.copy2(exe,os.environ['IDE_WRITE_BINARY'])
 if not os.environ.get('IDE_WRITE_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env)

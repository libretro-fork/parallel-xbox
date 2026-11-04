#!/usr/bin/env python3
"""EEPROM custom-state hashes and deterministic complete images."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
base=os.environ.get('EEPROM_SHA1_BASELINE')
def source(name):
 return subprocess.check_output(['git','-C',str(r),'show',base+':'+name],text=True) if base else (r/name).read_text()
def clean(s):return re.sub(r'^#include[^\n]*\n','',s,flags=re.M)
header=clean(source('hw/xbox/eeprom_generation.h'))
production=clean(source('hw/xbox/eeprom_generation.c'))
if base:production=clean(source('util/sha1.h'))+clean(source('util/sha1.c'))+production
production=clean(source('util/rc4.h'))+clean(source('util/rc4.c'))+production
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',header).replace('/* PRODUCTION */',production).replace('/* CONTEXT */','SHA1Context' if base else 'struct sha1_state')
with tempfile.TemporaryDirectory(prefix='eeprom-sha1-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);l=r/'libretro-common'
 binary=os.environ.get('EEPROM_SHA1_BINARY',str(p/'test'))
 win='mingw' in cc or os.name=='nt'
 flags=shlex.split(os.environ.get('EEPROM_SHA1_CFLAGS','-static' if win else '-fsanitize=address,undefined'))
 subprocess.run([cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Wno-sign-compare','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(l/'include'),*flags,str(p/'test.c'),str(l/'hash/lrc_hash.c'),str(l/'features/features_cpu.c'),'-pthread','-o',binary],check=True)
 if not os.environ.get('EEPROM_SHA1_SKIP_RUN'):
  text=subprocess.check_output([binary,str(p/'eeprom.bin')],text=True);print(text,end='')
  assert "EEPROM_SHA1=25e947cce24412876c83ee2df40b06f15204dbadf8386ad89b13a86a44e077f7" in text

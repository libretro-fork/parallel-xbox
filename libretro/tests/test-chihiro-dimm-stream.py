#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production DIMM serializer and memory cursor against v1 oracles."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
s=(r/'hw/xbox/chihiro/chihiro.c').read_text()
def function(signature):
 a=s.index(signature);z=s.index('{',a)+1;depth=1
 while depth:
  depth+=(s[z]=='{')-(s[z]=='}');z+=1
 return s[a:z]
a=s.index('#define DIMM_MIG_MAGIC');z=s.index('static ChihiroDimmMig',a)
types=s[a:z]
pieces=[function(x) for x in ('static bool chihiro_dimm_page_as_image(',
 'static int chihiro_dimm_pre_save(', 'static int chihiro_dimm_post_save(',
 'static int chihiro_dimm_put_blob(', 'static int chihiro_dimm_get_blob(',
 'static int chihiro_dimm_post_load(')]
common=(r/'libretro/tests/test-qemu-file-memory.c').read_text().split('/* PRODUCTION */')[0]
h=(r/'migration/qemu-file.h').read_text();a=h.index('struct QEMUFile {');z=h.index('\n};',a)+3
common=common.replace('/* STRUCT */',h[a:z])
cursor=(r/'migration/qemu-file.c').read_text();cursor=cursor[cursor.index('/* Saved-state cursors'):]
test=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* DIMM */','\n\n'.join(pieces))
with tempfile.TemporaryDirectory(prefix='dimm-stream-') as t:
 p=Path(t);(p/'test.c').write_text(common+cursor+'\n#undef memcpy\n'+test)
 binary=Path(os.environ.get('DIMM_STREAM_BINARY',str(p/'test')))
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter']
 cmd+=shlex.split(os.environ.get('DIMM_STREAM_CFLAGS',''))+[str(p/'test.c'),'-o',str(binary)]
 subprocess.run(cmd,check=True)
 if not os.environ.get('DIMM_STREAM_SKIP_RUN'):
  subprocess.run([str(binary)],check=True,timeout=30)

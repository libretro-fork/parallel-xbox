# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production shuffle emitter against a lane-selection oracle."""
from pathlib import Path
import subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();s=(repo/'target/i386/tcg/emit.c.inc').read_text()
a=s.index('static void gen_VSHUF(');body=s[a:s.index('static void gen_VUCOMI(',a)]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
with tempfile.TemporaryDirectory(prefix='sse-broadcast-') as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/'test.exe'
 for endian in [0,1]:
  subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-DHOST_BIG_ENDIAN='+str(endian),str(p/'test.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)

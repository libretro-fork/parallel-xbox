# SPDX-License-Identifier: GPL-2.0-or-later
"""Test actual idle-wait interrupt ordering against the existing handler API."""
from pathlib import Path
import subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve()
s=(repo/'accel/tcg/cpu-exec.c').read_text(encoding='utf-8',errors='surrogateescape')
a=s.index('static bool cpu_kernel_idle_wait(')
body=s[a:s.index('static inline void cpu_loop_exec_tb(',a)]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
with tempfile.TemporaryDirectory(prefix='idle-interrupts-') as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/'test.exe'
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

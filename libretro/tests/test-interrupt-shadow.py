# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile production x86 exit generation and generic chaining guards."""
from pathlib import Path
import subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve()
def extract(path,start,end):
 s=(repo/path).read_text(encoding='utf-8',errors='surrogateescape');a=s.index(start);return s[a:s.index(end,a)]
code=Path(__file__).with_suffix('.c').read_text()
for marker,path,start,end in [
 ('FLAGS','target/i386/tcg/translate.c','static bool gen_exit_tb_flags(','static CCPrepare gen_prepare_eflags_c'),
 ('LOOKUP','tcg/tcg-op.c','void tcg_gen_lookup_and_goto_ptr(void)','void tcg_gen_goto_ptr('),
 ('USE_GOTO_TB','accel/tcg/translator.c','bool translator_use_goto_tb(','void translator_loop('),
 ('GOTO_TB','tcg/tcg-op.c','void tcg_gen_goto_tb(unsigned idx)','void tcg_gen_lookup_and_goto_ptr(void)'),
 ('EOB','target/i386/tcg/translate.c','static void\ngen_eob(','/* Jump to eip+diff')]:
 code=code.replace('/* '+marker+' */',extract(path,start,end))
with tempfile.TemporaryDirectory(prefix='interrupt-shadow-') as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/'test.exe'
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

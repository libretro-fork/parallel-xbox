# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production x86 branch fallback and generic page guard."""
from pathlib import Path
import subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve()
s=(repo/'target/i386/tcg/translate.c').read_text(encoding='utf-8',errors='surrogateescape')
a=s.index('static void gen_jmp_rel(',s.index('/* Jump to eip+diff, truncating the result to OT. */'))
body=s[a:s.index('/* Jump to eip+diff, truncating to the current code size.',a)]
s=(repo/'accel/tcg/translator.c').read_text();a=s.index('bool translator_use_goto_tb(')
guard=s[a:s.index('void translator_loop(',a)]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body).replace('/* PAGE_GUARD */',guard)
with tempfile.TemporaryDirectory(prefix='two-page-exit-') as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/'test.exe'
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

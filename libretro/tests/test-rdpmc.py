# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production RDPMC helper and translator with fault/intercept oracles."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo=Path(sys.argv[1]).resolve()


def function(path,name):
    source=(repo/path).read_text()
    m=re.search(r'^(?:static )?void '+name+r'\([^;{]*\)\s*\{',source,re.M)
    assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]


declaration=re.search(r'^DEF_HELPER[^\n]*\(rdpmc,[^\n]*',
                      (repo/'target/i386/helper.h').read_text(),re.M)[0]
# A returning helper writes EAX/EDX: noreturn or NO_WG would misdescribe it
# to TCG even if the C helper itself returned the right values.
assert declaration=='DEF_HELPER_1(rdpmc, void, env)',declaration
code=Path(__file__).with_suffix('.c').read_text()
code=code.replace('/* HELPER */',function('target/i386/tcg/misc_helper.c','helper_rdpmc'))
code=code.replace('/* TRANSLATOR */',function('target/i386/tcg/emit.c.inc','gen_RDPMC'))
with tempfile.TemporaryDirectory(prefix='rdpmc-') as directory:
    p=Path(directory);(p/'test.c').write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                    '-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
                    str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=30)

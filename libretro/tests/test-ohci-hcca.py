# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production OHCI frame publication with interleaved guest accesses."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
s=(r/'hw/usb/hcd-ohci.c').read_text()
def function(name):
    m=re.search(r'^static[^\n]*\b'+name+r'\(',s,re.M)
    assert m,name
    start=s.index('{',m.start()); end=start+1; depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}'); end+=1
    return s[m.start():end]+'\n'
types=s[s.index('struct ohci_hcca {'):s.index('#define ED_WBACK_OFFSET')]
types+='\n'.join(re.findall(r'^#define OHCI_(?:INTR|CTL|STATUS)_[^\n]+',s,re.M))
body='\n'.join(function(n) for n in ['ohci_put_hcca','ohci_eof_timer','ohci_sof','ohci_process_lists','ohci_frame_boundary'])
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* PRODUCTION */',body)
if os.name!='nt':
    import resource
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='ohci-hcca-') as d:
    p=Path(d); src=p/'test.c'; binary=os.environ.get('OHCI_HCCA_BINARY',str(p/'test'))
    args=cc+['-O2','-std=c11','-Wall','-Werror']+shlex.split(os.environ.get('OHCI_HCCA_CFLAGS','-fsanitize=address,undefined'))
    def build(text):
        src.write_text(text)
        subprocess.run(args+[str(src),'-o',binary],check=True)
    build(code)
    if not os.environ.get('OHCI_HCCA_SKIP_RUN'):
        runner=shlex.split(os.environ.get('OHCI_HCCA_RUNNER',''))
        subprocess.run(runner+[binary],check=True,timeout=120)
        for name,mutant in [
            ('early completion interrupt',code.replace('done = true;', 'ohci_set_interrupt(ohci, OHCI_INTR_WD); done = true;')),
            ('stale head writeback',code.replace('done ? HCCA_WRITEBACK_SIZE :', 'true ? HCCA_WRITEBACK_SIZE :')),
            ('interrupt after failed DMA',code.replace('ohci_die(ohci);\n        return;\n    }\n    ohci_set_interrupt', 'ohci_die(ohci);\n    }\n    ohci_set_interrupt'))]:
            assert mutant!=code,name
            build(mutant)
            result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
            assert result.returncode and b'Assertion' in result.stdout,(name,result.stdout)
            print('Required negative control rejected:',name,flush=True)

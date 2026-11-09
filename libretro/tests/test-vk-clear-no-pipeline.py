# SPDX-License-Identifier: GPL-2.0-or-later
"""Run production clear/pass/draw setup, including a first clear with no PSO."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
def function(name):
    m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\(',s,re.M);assert m,name
    end=s.index('{',m.start())+1;depth=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[m.start():end]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(function(n) for n in ('clear_needs_color_draw','begin_pre_draw','begin_draw','end_draw')))
with tempfile.TemporaryDirectory(prefix='clear-no-pso-') as d:
    p=Path(d);src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
    src.write_text(code);subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
    mutants=[
        ('attachment clear creates PSO',code.replace('if (pg->clearing && !clear_needs_color_draw(r)) {','if (false) {',1)),
        ('lost vertex-input invalidation',code.replace('        r->vi_last.valid = false;','        r->vi_last.valid = true;',1)),
        ('stale render pass',code.replace('r->render_pass = render_pass;','(void)render_pass;',1)),
        ('partial color clear loses draw',code.replace('return r->color_binding && color && color != NV097_CLEAR_SURFACE_COLOR;','(void)color; return false;',1)),
        ('lost clear draw scope',code.replace('        r->in_draw = true;','        r->in_draw = false;',1)),
    ]
    for name,mutant in mutants:
        assert mutant!=code,name;src.write_text(mutant);subprocess.run(cmd,check=True)
        bad=subprocess.run([str(exe)],capture_output=True);assert bad.returncode!=0 and b'FAIL clear-pipeline' in bad.stderr,name
        print('Required negative control rejected:',name)

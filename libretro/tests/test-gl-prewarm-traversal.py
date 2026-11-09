# SPDX-License-Identifier: GPL-2.0-or-later
"""Production incremental cache traversal: quota, ownership and cancellation."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
def function(name):
    m=re.search(r'^static void '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
    a=s.index('{',m.start());b=a+1;depth=1
    while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[m.start():b]
body='\n'.join(function(n) for n in ('module_prewarm_finish','module_prewarm_start','module_prewarm_pump'))
fixture=Path(__file__).with_suffix('.c').read_text()
header=(r/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text()
layout=re.search(r'/\* Boot prewarm of the on-disk module cache\. \*/\s*(struct \{.*?\} prewarm;)',header,re.S)
assert layout
fixture=fixture.replace('/* PREWARM */',layout[1])
finalize=s[s.index('void pgraph_gl_finalize_shaders('):s.index('static void shader_write_to_disk(')]
assert 'module_prewarm_finish(r);' in finalize
assert 'prewarm.files' not in s and 'prewarm.idx' not in s
mutants={
    'unbounded traversal':body.replace('items < max_items','items < 1000000'),
    'leaked directory owner':body.replace('g_dir_close(r->prewarm.root);','(void)r;'),
    'retained borrowed name':body.replace('g_strdup_printf("%s/%s", r->prewarm.base, sub)','(char *)sub'),
    'leaked file path':body.replace('g_free(path);','(void)path;'),
    'zero quota still opens cache':body.replace('if (!max_items)', 'if (false)'),
}
with tempfile.TemporaryDirectory(prefix='gl-prewarm-') as tmp:
    p=Path(tmp)
    for name,code in [('positive',body),*mutants.items()]:
        assert name=='positive' or code!=body,name
        c=p/'test.c';exe=p/'test';c.write_text(fixture.replace('/* PRODUCTION */',code))
        subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('GL_PREWARM_CFLAGS','-fsanitize=address,undefined')),str(c),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
        if name=='positive':
            assert result.returncode==0,result.stderr;print(result.stdout.strip())
        else:
            assert result.returncode!=0,'bad traversal passed: '+name
            print('Rejected: '+name)

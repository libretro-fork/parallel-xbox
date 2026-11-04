# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production module-cache admission, mapped input and failure cleanup."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=Path(os.environ.get('GL_MODULE_LOAD_SOURCE',repo/'hw/xbox/nv2a/pgraph/gl/shaders.c')).read_text()
def function(name):
    m=re.search(r'^static (?:bool|const void \*) *'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
    a=s.index('{',m.start());b=a+1;depth=1
    while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[m.start():b]
parser=s[s.index('typedef struct FileCursor'):s.index('static bool shader_read_cache_version')]
version=re.search(r'static const char shader_codegen_version\[\] = "[^"]+";',s)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',version+'\n'+parser+'\n'+'\n'.join(function(n) for n in ['shader_read_cache_version','module_read_header_view','module_load_from_disk']))
with tempfile.TemporaryDirectory(prefix='gl-module-load-') as tmp:
    p=Path(tmp);c=p/'test.c';c.write_text(code)
    exe=Path(os.environ.get('GL_MODULE_LOAD_BINARY',p/'test'))
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter',*shlex.split(os.environ.get('GL_MODULE_LOAD_CFLAGS','-fsanitize=address,undefined')),str(c),'-o',str(exe)],check=True)
    if not os.environ.get('GL_MODULE_LOAD_SKIP_RUN'):subprocess.run([str(exe)],check=True,timeout=30)

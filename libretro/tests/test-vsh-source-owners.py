#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Track actual MString record owners across all vertex source generation.

The original inline allocation/free bodies call a test recorder; the real
allocator and real GString payload implementation stay in use. This checks
CPU temporary/returned-source lifetime, not driver or global allocations.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


def prepare(root, directory, mutation=False):
    original = (root/'libretro/tests/test-spv-vsh-gpu.c').read_text()
    pieces = {
        'TOKEN_HELPERS': original[original.index('static void token('):original.index('static void pack(')],
        'KEY_HELPERS': original[original.index('/* Test-only guard'):original.index('int main(')],
        'CORPUS': original[original.index(' for(unsigned test=first;'):original.index('  GlslcResult modules[2];')],
    }
    code = Path(__file__).with_suffix('.c').read_text()
    for name, body in pieces.items():
        marker = '/* '+name+' */'
        assert code.count(marker)==1
        code = code.replace(marker,body)
    (directory/'driver.c').write_text(code)
    (directory/'owners.c').write_text('''#include "'''+(root/'include/qemu/mstring.h').as_posix()+'''"
static MString *owners[128];
void *mstring_test_alloc(gsize size){assert(size==sizeof(MString));MString *p=g_malloc(size);unsigned i=0;while(i<128 && owners[i])i++;assert(i<128);owners[i]=p;return p;}
void mstring_test_free(void *p){unsigned i=0;while(i<128 && owners[i]!=p)i++;assert(i<128);owners[i]=NULL;g_free(p);}
size_t mstring_test_live(MString *returned){size_t n=0;bool found=false;for(unsigned i=0;i<128;i++)if(owners[i]){n++;if(owners[i]==returned)found=true;}assert(!returned || found);return n;}
''')
    script = (root/'libretro/tests/test-spv-vsh-gpu.py').read_text()
    needle = " (d/'reference.c').write_text(ref)"
    assert script.count(needle)==1
    has_interpreter='MString *pgraph_glsl_gen_vsh_interpreter(' in (root/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
    addition = ''
    if has_interpreter:
        addition += " ref+=function(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c','pgraph_glsl_gen_vsh_interpreter')+'\\n'\n"
    has_dynamic='MString *pgraph_glsl_gen_vsh_interpreter_dynamic(' in (root/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
    if has_dynamic:
        addition += " ref+=function(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c','pgraph_glsl_gen_vsh_interpreter_dynamic')+'\\n'\n"
    if mutation:
        addition += " assert ref.count('mstring_unref(uniforms);')==1\n ref=ref.replace('mstring_unref(uniforms);','(void)uniforms;')\n"
    shim = '''#include "qemu/osdep.h"
#include "glib.h"
void *mstring_test_alloc(gsize);
void mstring_test_free(void *);
#define g_malloc mstring_test_alloc
#define g_free mstring_test_free
#include "'''+(root/'include/qemu/mstring.h').as_posix()+'''"
#undef g_malloc
#undef g_free
size_t mstring_test_live(MString *);
'''
    addition += " (d/'qemu/mstring.h').write_text("+repr(shim)+")\n"
    script = script.replace(needle,addition+needle)
    needle = ' sources=['
    assert script.count(needle)==1
    script = script.replace(needle,' sources=[Path('+repr(str(directory/'owners.c'))+'),')
    if has_interpreter:
        needle=" flags=[cc,'-O2',"
        assert script.count(needle)==1
        script=script.replace(needle," flags=[cc,'-DTEST_INTERPRETER_OWNERS','-O2',")
    if has_dynamic:
        needle=" flags=[cc,"
        assert script.count(needle)==1
        script=script.replace(needle," flags=[cc,'-DTEST_DYNAMIC_INTERPRETER_OWNERS',")
    if os.environ.get('SHADER_OWNER_SANITIZE'):
        needle = " flags=[cc,"
        assert script.count(needle)==1
        script = script.replace(needle," flags=[cc,'-fsanitize=address,undefined','-fno-omit-frame-pointer',")
    (directory/'driver.py').write_text(script)


def run(root, compiler, directory, mutation=False):
    prepare(root,directory,mutation)
    environment=dict(os.environ)
    executable=shutil.which(compiler) or compiler
    environment['PATH']=str(Path(executable).resolve().parent)+os.pathsep+environment.get('PATH','')
    if mutation:
        environment['SPV_VSH_GPU_RANGE']='0:1'
    log=directory/'result.log'
    with log.open('w') as stream:
        result=subprocess.run([sys.executable,str(directory/'driver.py'),str(root),compiler],env=environment,stdout=stream,stderr=subprocess.STDOUT)
    text=log.read_text(errors='replace')
    if mutation:
        assert result.returncode!=0 and 'Assertion' in text and 'mstring_test_live(text)==1' in text,text
        print('Required source-owner leak control rejected',flush=True)
    else:
        print(text,end='',flush=True)
        assert result.returncode==0,result.returncode


def main():
    root=Path(sys.argv[1]).resolve()
    compiler=sys.argv[2] if len(sys.argv)>2 else 'cc'
    with tempfile.TemporaryDirectory(prefix='vsh-source-owners-') as temporary:
        directory=Path(temporary)
        for name,mutation in [('positive',False),('missing_release',True)]:
            p=directory/name;p.mkdir();run(root,compiler,p,mutation)


if __name__=='__main__':
    main()

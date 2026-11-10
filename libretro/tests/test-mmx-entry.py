# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare inline MMX entry and state-transition generators to the helpers."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
def function(path,key):
    s=(r/path).read_text();a=s.index(key);b=s.index('{',a)+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
core='target/i386/tcg/translate.c'; emit='target/i386/tcg/emit.c.inc'
parts=[function(core,'static void gen_enter_mmx('),function(emit,'static void gen_EMMS('),
       function(emit,'static void gen_FXRSTOR('),function(emit,'static void gen_XRSTOR(')]
helpers='\n'.join(function('target/i386/tcg/fpu_helper.c','void helper_'+n+'(') for n in ['enter_mmx','emms'])
s=(r/core).read_text()
assert 'mmx_entered = false;' in function(core,'static void gen_x87(')
assert 'dc->mmx_entered = false;' in function(core,'static void i386_tr_init_disas_context(')
rollback=function(core,'static void i386_tr_translate_insn(')
assert rollback.index('orig_mmx_entered = dc->mmx_entered')<rollback.index('sigsetjmp(')
assert rollback.index('case 2:')<rollback.index('dc->mmx_entered = orig_mmx_entered;')<rollback.index('tcg_remove_ops_after(')
assert '[0x0e] = X86_OP_ENTRY0(EMMS,' in (r/'target/i386/tcg/decode-new.c.inc').read_text()
decode=(r/'target/i386/tcg/decode-new.c.inc').read_text()
assert decode.index('gen_enter_mmx(s);')<decode.index('gen_lea_modrm(s, &decode);')
declarations=(r/'target/i386/helper.h').read_text()
assert 'DEF_HELPER_2(fxrstor, void, env, tl)' in declarations
assert 'DEF_HELPER_3(xrstor, void, env, tl, i64)' in declarations
code=Path(__file__).with_suffix('.c').read_text().replace('/* HELPERS */',helpers).replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='mmx-entry-') as d:
    p=Path(d);src=p/'test.c';binary=os.environ.get('MMX_ENTRY_BINARY',str(p/'test'))
    args=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')+['-O2','-std=gnu11','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-Wno-unused-parameter']
    args+=shlex.split(os.environ.get('MMX_ENTRY_CFLAGS','-fsanitize=address,undefined'))
    src.write_text(code);subprocess.run(args+[str(src),'-o',binary],check=True)
    if not os.environ.get('MMX_ENTRY_SKIP_RUN'):
        runner=shlex.split(os.environ.get('MMX_ENTRY_RUNNER',''))
        subprocess.run(runner+[binary],check=True,timeout=30)
        for name,mutant in [('missing x87 publication',code.replace('    gen_flush_fp(s);','',1)),
                            ('entry repeated for every instruction',code.replace('if (s->mmx_entered)', 'if (false)')),
                            ('EMMS invalidation omitted',code.replace('    s->mmx_entered = false;','',1))]:
            assert mutant!=code,name
            src.write_text(mutant);subprocess.run(args+[str(src),'-o',binary],check=True)
            result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            assert result.returncode and b'Assertion' in result.stdout,(name,result.stdout)
            print('Required negative control rejected:',name,flush=True)

# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production generated lookup decisions against an identity oracle."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve()
def fn(file,signature):
 s=(r/file).read_text();a=s.index(signature);end=s.index('{',a)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[a:end]
h='accel/tcg/tb-jmp-cache.h';t='accel/tcg/translator.c'
layout=fn(h,'typedef struct CPUJumpCache {')+' CPUJumpCache;'
prod=fn(t,'static void gen_tb_flags_check(')+'\n'+fn(t,'static void gen_lookup_and_goto_ptr(')
has_epoch='mmu_epoch' in layout
code=('#define HAS_CACHE_EPOCH %d\n' % has_epoch)+Path(__file__).with_suffix('.c').read_text().replace('/* LAYOUT */',layout).replace('/* PRODUCTION */',prod)
with tempfile.TemporaryDirectory(prefix='generated-mmu-cache-') as directory:
 p=Path(directory);exe=p/'test.exe'
 flags=[sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
 flags+=shlex.split(os.environ.get('TB_MMU_CFLAGS',''))
 (p/'test.c').write_text(code)
 subprocess.run(flags+[str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 mutations=[
  ('tcg_gen_brcond_i64(TCG_COND_NE, t, pc, slow);','tcg_gen_brcond_i64(TCG_COND_NE, t, t, slow);'),
  ('tcg_gen_brcondi_i32(TCG_COND_NE, t32, cflags, slow);','tcg_gen_brcond_i32(TCG_COND_NE, t32, t32, slow);'),
 ]
 if has_epoch:mutations+=[ 
  ('tcg_gen_ld32u_i64(t, ent, offsetof(CPUJumpCache, array[0].pc));','tcg_gen_ld_i64(t, ent, offsetof(CPUJumpCache, array[0].pc));'),
  ('tcg_gen_brcond_i32(TCG_COND_NE, t32, epoch, slow);','tcg_gen_brcond_i32(TCG_COND_NE, t32, t32, slow);'),
 ]
 for old,new in mutations:
  assert old in code
  (p/'negative.c').write_text(code.replace(old,new,1))
  subprocess.run(flags+[str(p/'negative.c'),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True)
  assert result.returncode and b'generated MMU:' in result.stderr
 print('Negative controls rejected: PC and cflags bypasses'+('; old width and epoch bypasses' if has_epoch else ''))

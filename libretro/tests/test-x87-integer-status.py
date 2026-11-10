# SPDX-License-Identifier: GPL-2.0-or-later
"""Run real integer, round, BCD, scale and exception-merge helpers against SoftFloat."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
s=(r/'target/i386/tcg/fpu_helper.c').read_text()
def function(name):
 m=re.search(r'^(?:static )?(?:inline )?(?:void|int|int32_t|int64_t) '+name+r'\(',s,re.M);assert m,name
 start=s.index('{',m.start());end=start+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
body='\n'.join(re.findall(r'^#define (?:FPUS_\w+|FPUC_EM) .*',s,re.M))+'\n'
body+='\n'.join(function(n) for n in ['fpu_set_exception','save_exception_flags','merge_softfloat_exception_flags','helper_fist_ST0','helper_fistl_ST0','helper_fistll_ST0','helper_fistt_ST0','helper_fisttl_ST0','helper_fisttll_ST0','helper_frndint','helper_fbst_ST0','helper_fscale'])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
macros=subprocess.run(cc+['-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
host='aarch64' if '__aarch64__' in macros else 'x86_64'
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='x87-integer-status-') as d:
 p=Path(d);src=p/'test.c';binary=os.environ.get('X87_INT_BINARY',str(p/'test'))
 args=cc+['-O2','-std=gnu11','-fno-strict-aliasing','-fwrapv','-D_GNU_SOURCE','-Wall','-Werror','-Wno-unused-parameter']
 for path in ['libretro/gen','.','libretro/glib','include','host/include/'+host,'host/include/generic','libretro-common/include']:args+=['-I'+str(r/path)]
 args+=shlex.split(os.environ.get('X87_INT_CFLAGS','-fsanitize=address,undefined'))
 subprocess.run(args+['-c',str(r/'fpu/softfloat.c'),'-o',str(p/'softfloat.o')],check=True)
 def build(text):
  src.write_text(text);subprocess.run(args+[str(src),str(p/'softfloat.o'),'-lm','-o',binary],check=True)
 build(code)
 if not os.environ.get('X87_INT_SKIP_RUN'):
  runner=shlex.split(os.environ.get('X87_INT_RUNNER',''))
  subprocess.run(runner+[binary],check=True,timeout=120)
  for name,mutant in [
   ('hard-FPU merge remains disabled',code.replace('merge_softfloat_exception_flags(env, old_flags);','if (false) merge_softfloat_exception_flags(env, old_flags);')),
   ('new exceptions discarded',code.replace('int new_flags = get_float_exception_flags(&env->fp_status);','int new_flags = 0;')),
   ('prior sticky flags lost',code.replace('float_raise(old_flags, &env->fp_status);','float_raise(0, &env->fp_status);')),
   ('truncate follows guest RC',code.replace('floatx80_to_int32_round_to_zero(ST0,','floatx80_to_int32(ST0,')),
   ('round exceptions discarded',code.replace(function('helper_frndint'),function('helper_frndint').replace('merge_softfloat_exception_flags(env, old_flags);','if (false) merge_softfloat_exception_flags(env, old_flags);'))),
   ('BCD exceptions discarded',code.replace(function('helper_fbst_ST0'),function('helper_fbst_ST0').replace('merge_softfloat_exception_flags(env, old_flags);','if (false) merge_softfloat_exception_flags(env, old_flags);'))),
   ('scale exceptions discarded',code.replace(function('helper_fscale'),function('helper_fscale').replace('merge_softfloat_exception_flags(env, old_flags);','if (false) merge_softfloat_exception_flags(env, old_flags);')))]:
   assert mutant!=code,name
   build(mutant);result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
   assert result.returncode and b'Assertion' in result.stdout,(name,result.stdout)
   print('Required negative control rejected:',name,flush=True)

# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep reference scalar-dot output mapping before RGB construction."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
with tempfile.TemporaryDirectory(prefix='psh-dot-mapping-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <stdlib.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'hw/xbox/nv2a/pgraph/spv'),'-I'+str(r/'deps/spirv-headers/include')]
 source=(r/'hw/xbox/nv2a/pgraph/spv/psh.c').read_text()
 before='    ab=map_output(c,ab,flags&0x38,alpha || ab_dot);\n    cd=map_output(c,cd,flags&0x38,alpha || cd_dot);\n    if(ab_dot)ab=splat(c,c->v3,ab,3);\n    if(cd_dot)cd=splat(c,c->v3,cd,3);'
 after='    if(ab_dot)ab=splat(c,c->v3,ab,3);\n    if(cd_dot)cd=splat(c,c->v3,cd,3);\n    ab=map_output(c,ab,flags&0x38,alpha);\n    cd=map_output(c,cd,flags&0x38,alpha);'
 assert source.count(before)==1
 for control in [False,True]:
  src=d/'psh.c';src.write_text(source.replace(before,after,1) if control else source)
  obj=d/'psh.o';exe=d/'test'
  subprocess.run(flags+['-Dmalloc=psh_test_allocate','-c',str(src),'-o',str(obj)],check=True)
  subprocess.run(flags+[str(Path(__file__).with_suffix('.c')),str(obj),'-lSPIRV-Tools','-lstdc++','-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True,text=True)
  if control:
   assert result.returncode and 'mapping-order failure: validated module' in result.stderr,result.stdout+result.stderr
   print('Required negative control rejected: valid SPIR-V vectorizes dot before output mapping')
  else:
   assert result.returncode==0,result.stdout+result.stderr
   print(result.stdout,end='')

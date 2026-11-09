# SPDX-License-Identifier: GPL-2.0-or-later
"""Real input-ring lends, wrap fallback and release after the last compiler read."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text()
m=re.search(r'^static bool vk_spec_process_input\(void\).*?^\}',s,re.M|re.S);assert m
body=m[0];code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='worker-input-view-') as d:
    p=Path(d);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
    src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-pthread','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),str(src),str(r/'libretro-common/queues/retro_spsc.c'),'-o',str(exe)]
    src.write_text(code);subprocess.run(cmd,env=env,check=True);subprocess.run([str(exe)],env=env,check=True,timeout=60)
    controls=[
        ('release before last compiler read',body.replace('vk_spec_compile_state(record);','retro_spsc_read_end(&vk_spec_in, sizeof(ShaderState));\n            vk_spec_compile_state(record);',1)),
        ('unconditional input copy',body.replace('contiguous >= sizeof(ShaderState)', 'contiguous > vk_spec_in.capacity',1)),
        ('wrong lend extent',body.replace('retro_spsc_read_end(&vk_spec_in, sizeof(ShaderState));','retro_spsc_read_end(&vk_spec_in, sizeof(ShaderState) - 1);',1)),
        ('cancellation ignored',body.replace('if (!retro_atomic_load_acquire_int(&vk_spec_cancel))','if (true)',1)),
    ]
    for name,mutant in controls:
        assert mutant!=body,name;src.write_text(code.replace(body,mutant,1));subprocess.run(cmd,env=env,check=True)
        bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60)
        assert bad.returncode==1 and b'FAIL ' in bad.stderr,name
        print('Required negative control rejected:',name)

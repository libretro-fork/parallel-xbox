# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise the production Vulkan heap with a deterministic driver oracle."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
gpu='--gpu' in sys.argv[3:]
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='xemu-vk-memory-') as d:
    exe=Path(d)/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-type-limits',
        '-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),
        '-I'+str(r/'deps/volk'),'-I'+str(r/'deps/vulkan-headers'),
        str(r/'libretro/tests/test-vk-memory.c'),str(r/'hw/xbox/nv2a/pgraph/vk/memory.c'),
        '-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env,timeout=90)
    if gpu:
        helper=(r/'libretro/tests/spv-clear-vulkan.h').read_text()
        helper=helper[:helper.index('static VkPipeline pipeline(')]
        helper=helper.replace('Utility GPU:','Memory heap GPU:')
        source=(r/'libretro/tests/test-vk-memory-gpu.c').read_text().replace('/* GPU HELPERS */',helper)
        test=Path(d)/'gpu.c';test.write_text(source)
        subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
            '-Wno-unused-function','-Wno-unused-variable','-Wno-type-limits',
            '-I'+str(r),'-I'+str(r/'deps/volk'),'-I'+str(r/'deps/vulkan-headers'),
            str(test),str(r/'hw/xbox/nv2a/pgraph/vk/memory.c'),str(r/'deps/volk/volk.c'),
            *([] if os.name=='nt' else ['-ldl']),'-o',str(exe)],check=True,env=env)
        subprocess.run([str(exe)],check=True,env=env,timeout=90)

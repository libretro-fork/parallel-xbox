# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise the production Vulkan heap with a deterministic driver oracle."""
from pathlib import Path

vk_loader_root = Path(__file__).resolve().parents[2]
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
gpu='--gpu' in sys.argv[3:]
memory_source=Path(os.environ.get('VK_MEMORY_SOURCE',r/'hw/xbox/nv2a/pgraph/vk/memory.c'))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='xemu-vk-memory-') as d:
    exe=Path(d)/('test.exe' if os.name=='nt' else 'test')
    # Historical candidates use the same PFN declarations via the old header.
    # Normalize only that include; keep the allocator body under test unchanged.
    memory_text=memory_source.read_text()
    if '#include <volk.h>' in memory_text:
        memory_source=Path(d)/'memory.c'
        memory_source.write_text(memory_text.replace('#include <volk.h>', '#include <vk_dispatch.h>'))

    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-type-limits',
        '-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),
        '-I'+str(r/'hw/xbox/nv2a/pgraph/vk'),'-I'+str(vk_loader_root/'libretro'),'-I'+str(r/'deps/vulkan-headers'),
        str(vk_loader_root/'libretro/tests/test-vk-memory.c'),str(memory_source),
        '-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env,timeout=90)
    if gpu:
        helper=(vk_loader_root/'libretro/tests/spv-clear-vulkan.h').read_text()
        helper=helper[:helper.index('static VkPipeline pipeline(')]
        helper=helper.replace('Utility GPU:','Memory heap GPU:')
        source=(vk_loader_root/'libretro/tests/test-vk-memory-gpu.c').read_text().replace('/* GPU HELPERS */',helper)
        test=Path(d)/'gpu.c';test.write_text(source)
        subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
            '-Wno-unused-function','-Wno-unused-variable','-Wno-type-limits',
            '-I'+str(r),'-I'+str(r/'hw/xbox/nv2a/pgraph/vk'),'-I'+str(vk_loader_root/'libretro'),'-I'+str(r/'deps/vulkan-headers'),
            str(test),str(memory_source),str(vk_loader_root/'libretro/vk_dispatch.c'),str(vk_loader_root/'libretro/tests/vk-test-loader.c'),
            *([] if os.name=='nt' else ['-ldl']),'-o',str(exe)],check=True,env=env)
        subprocess.run([str(exe)],check=True,env=env,timeout=90)

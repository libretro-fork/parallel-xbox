# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production scanout upload batch regions, ownership and staging bytes."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(root/'hw/xbox/nv2a/pgraph/vk/surface.c').read_text()
a=source.index('        VkDeviceSize offset = staged_offset + staged_used;',source.index('static void upload_scanout_background'))
b=source.index('        vkCmdCopyBufferToImage',a)
begin=source.index('static void upload_scanout_background')
pre_start=source.index('    pgraph_vk_upload_source_barrier(pg, cmd, from_vram, scan->start,',begin)
pre_end=source.index(';',pre_start)+1
post_start=source.index('    VkBufferMemoryBarrier consumed =',begin)
post_end=source.index('    pgraph_vk_transition_image_layout',post_start)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',source[a:b]).replace('/* PRE BARRIER */',source[pre_start:pre_end]).replace('/* POST BARRIER */',source[post_start:post_end])
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='vk-scanout-upload-',dir=root.parent) as tmp:
 t=Path(tmp);(t/'test.c').write_text(code);binary=os.environ.get('VK_SCANOUT_UPLOAD_BINARY',str(t/'test'))
 flags=shlex.split(os.environ.get('VK_SCANOUT_UPLOAD_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(root),'-I'+str(root/'deps/vulkan-headers'),str(t/'test.c'),'-o',binary]+(['-static'] if win else []),check=True)
 if not os.environ.get('VK_SCANOUT_UPLOAD_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)

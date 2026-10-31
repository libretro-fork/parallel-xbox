# SPDX-License-Identifier: GPL-2.0-or-later
"""Production NVNet TX and network queue, with bounded DMA/backend models."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
nic=(r/'hw/xbox/mcpx/nvnet/nvnet.c').read_text();queue=(r/'net/queue.c').read_text()
def function(source,name):
 m=re.search(r'^(?:static )?(?:bool|void|ssize_t) '+name+r'\(',source,re.M);assert m,name
 a=source.index('{',m.start());z=a+1;depth=1
 while depth:
  depth+=(source[z]=='{')-(source[z]=='}');z+=1
 return source[m.start():z]
structures=queue[queue.index('struct NetPacket {'):queue.index('NetQueue *qemu_new_net_queue')]
production='\n'.join(function(queue,name) for name in ('qemu_net_queue_append','qemu_net_queue_deliver','qemu_net_queue_send','qemu_net_queue_flush'))
tx='\n'.join(function(nic,name) for name in ('send_packet_direct','dma_packet_from_guest'))
ring=nic[nic.index('struct RingDesc {'):nic.index('} QEMU_PACKED;')+len('} QEMU_PACKED;')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* QUEUE TYPES */',structures).replace('/* QUEUE */',production).replace('/* RING */',ring).replace('/* TX */',tx)
with tempfile.TemporaryDirectory(prefix='nvnet-borrow-') as t:
 p=Path(t);(p/'test.c').write_text(code);binary=Path(os.environ.get('NVNET_BORROW_BINARY',str(p/'test')))
 flags=shlex.split(os.environ.get('NVNET_BORROW_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
 subprocess.run([cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r),str(p/'test.c'),'-o',str(binary)],check=True)
 if not os.environ.get('NVNET_BORROW_SKIP_RUN'):
  subprocess.run([str(binary)],check=True,timeout=30)

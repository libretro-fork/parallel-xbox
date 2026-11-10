# SPDX-License-Identifier: GPL-2.0-or-later
"""Production NVNet RX readiness, descriptor DMA and completion checks."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
s=Path(os.environ.get('NVNET_RX_SOURCE',repo/'hw/xbox/mcpx/nvnet/nvnet.c')).read_text()
def function(name):
 m=re.search(r'^static (?:bool|void|ssize_t|uint32_t|uint16_t|struct RingDesc) '+name+r'\(',s,re.M);assert m,name
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
names=['get_rx_ring_size','link_up','dma_enabled','set_dma_idle','rx_enabled','update_current_rx_ring_desc_addr','advance_next_rx_ring_desc_addr','load_ring_desc','store_ring_desc','rx_buf_available']
if 'static bool nvnet_can_receive_desc(' in s:names+=['nvnet_can_receive_desc']
names+=['nvnet_can_receive','nvnet_flush_rx','rx_poll_timer','dma_packet_to_guest']
ring=s[s.index('struct RingDesc {'):s.index('} QEMU_PACKED;')+len('} QEMU_PACKED;')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* RING */',ring).replace('/* PRODUCTION */','\n'.join(function(n) for n in names))
with tempfile.TemporaryDirectory(prefix='nvnet-rx-') as directory:
 tmp=Path(directory);c=tmp/'test.c';c.write_text(code);exe=Path(os.environ.get('NVNET_RX_BINARY',str(tmp/'test')))
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-I'+str(repo),str(c),
 *shlex.split(os.environ.get('NVNET_RX_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error')),'-o',str(exe)],check=True)
 if not os.environ.get('NVNET_RX_SKIP_RUN'):
  runner=shlex.split(os.environ.get('NVNET_RX_RUNNER',''))
  subprocess.run(runner+[str(exe)],check=True,timeout=30)
  for label,old,new in [
   ('stopped VM DMA gate','!runstate_is_running() || nc->link_down','nc->link_down'),
   ('disconnected NIC gate','!runstate_is_running() || nc->link_down','!runstate_is_running()'),
   ('RX independent of TX backpressure','!runstate_is_running() || nc->link_down','!runstate_is_running() || nc->link_down || nc->tx_blocked')]:
   assert old in code,label
   c.write_text(code.replace(old,new))
   subprocess.run([cc,'-O2','-std=gnu11','-Wno-unused-parameter','-I'+str(repo),str(c),'-o',str(exe)],check=True)
   result=subprocess.run(runner+[str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
   assert result.returncode and b'Assertion' in result.stdout,(label,result.stdout)
   print('Required negative control rejected:',label,flush=True)

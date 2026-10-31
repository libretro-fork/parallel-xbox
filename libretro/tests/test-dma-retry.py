# SPDX-License-Identifier: GPL-2.0-or-later
"""Production DMA retry ownership/publication and queued callback retirement."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'system/physmem.c').read_text();m=(r/'system/memory.c').read_text();h=(r/'include/system/memory.h').read_text()
def function(source,name):
 match=re.search(r'^(?:static )?void\s+'+name+r'\([^;{]*\)\s*\{',source,re.M);assert match,name
 end=source.index('{',match.start())+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[match.start():end]
parts=[function(s,n) for n in ['address_space_unregister_map_client_do','address_space_notify_map_clients_owner','address_space_register_map_client','address_space_unregister_map_client','address_space_notify_map_clients']]
parts[2]=parts[2].replace('        QEMUBH *notify = qemu_bh_new','        test_stage(1);\n        QEMUBH *notify = qemu_bh_new').replace('retro_atomic_store_release_ptr(&as->map_client_bh, notify);','retro_atomic_store_release_ptr(&as->map_client_bh, notify);\n        test_stage(2);').replace('    /* Publish the notifier','    test_stage(3);\n    /* Publish the notifier')
parts[4]=parts[4].replace('            qemu_bh_schedule(notify);','            test_release_stage();\n            qemu_bh_schedule(notify);')
parts[2]=parts[2][:-1]+'    test_stage(4);\n}'
a=s.index('    retro_atomic_fetch_sub_64(&as->bounce_buffer_size, bounce->len);',s.index('void address_space_unmap('));b=s.index('    address_space_notify_map_clients(as);',a)+len('    address_space_notify_map_clients(as);')
release=s[a:b].replace('bounce->len','amount')
release='\n'.join(x for x in release.splitlines() if not any(y in x for y in ['bounce->magic','memory_region_unref','g_free(bounce)']))
client=re.search(r'typedef struct AddressSpaceMapClient \{.*?\n\} AddressSpaceMapClient;',h,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* CLIENT */',client).replace('/* PRODUCTION */','\n'.join(parts)+'\n'+function(m,'address_space_cancel_map_client_bh')).replace('/* RELEASE */',release)
with tempfile.TemporaryDirectory(prefix='dma-retry-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('DMA_RETRY_BINARY',str(p/'test'));l=r/'libretro-common'
 cmd=[cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-cast-function-type','-DHAVE_THREADS','-I'+str(r/'include'),'-I'+str(l/'include')]
 cmd+=shlex.split(os.environ.get('DMA_RETRY_CFLAGS','-fsanitize=address,undefined'))
 subprocess.run(cmd+[str(p/'test.c'),str(l/'rthreads/rthreads.c'),str(l/'features/features_cpu.c'),str(l/'time/rtime.c'),'-pthread','-o',binary],check=True)
 if not os.environ.get('DMA_RETRY_SKIP_RUN'):subprocess.run([binary],check=True,timeout=60)
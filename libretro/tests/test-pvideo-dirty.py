# SPDX-License-Identifier: GPL-2.0-or-later
"""Production RAM-client propagation and independent PVIDEO upload invalidation."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(s,name):
 m=re.search(r'^(?:static )?(?:void|bool|uint8_t)\s+\*?'+name+r'\(',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
ram=(r/'include/exec/ramlist.h').read_text();defs='\n'.join(line for line in ram.splitlines() if line.startswith('#define DIRTY_MEMORY_') and 'BLOCK' not in line)
s=(r/'system/physmem.c').read_text();parts=[function(s,n).replace('void physical_memory_set_dirty_range(', 'void physical_memory_set_dirty_range_impl(',1) for n in ['physical_memory_is_clean','physical_memory_range_includes_clean','physical_memory_set_dirty_range','physical_memory_clear_dirty_range']]
display=(r/'hw/xbox/nv2a/pgraph/vk/display.c').read_text()
util=(r/'hw/xbox/nv2a/pgraph/util.h').read_text()
for name in ['cliptobyte','pgraph_yuv422_store_rgba','pgraph_yuv422_to_rgba']:
 m=re.search(r'^(?:static\s+inline\s+)?(?:void|uint8_t) '+name+r'\(',util,re.M);assert m,name
 a=m.start();b=util.index('{',a)+1;depth=1
 while depth:depth+=(util[b]=='{')-(util[b]=='}');b+=1
 parts.append(util[a:b])
parts += [function(display,n) for n in ['convert_texture_data__CR8YB8CB8YA8','pgraph_vk_mark_pvideo_source_dirty','pvideo_upload_required','upload_pvideo_image']]
for name in ['vk/blit.c','vk/surface.c']:
 s=(r/'hw/xbox/nv2a/pgraph'/name).read_text();m=re.search(r'memory_region_set_client_dirty\(d->vram,[^;]*DIRTY_MEMORY_NV2A_TEX\);',s);assert m,name
 args=re.search(r'd->vram,(.*),\s*DIRTY_MEMORY_NV2A_TEX',m[0],re.S)[1]
 expected=re.sub(r'\s+','',args)
 assert re.sub(r'\s+','',f'pgraph_vk_mark_pvideo_source_dirty(d,{args});') in re.sub(r'\s+','',s),name
assert 'DIRTY_MEMORY_NV2A_PVIDEO' in function((r/'system/memory.c').read_text(),'memory_region_set_log')
assert 'display.pvideo.upload_valid = false' in (r/'hw/xbox/nv2a/pgraph/vk/renderer.c').read_text()
code=Path(__file__).with_suffix('.c').read_text().replace('/* CLIENTS */',defs).replace('/* PRODUCTION */','\n'.join(parts));env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
with tempfile.TemporaryDirectory(prefix='pvideo-dirty-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)]
 subprocess.run(command,check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=60)
 subprocess.run([str(exe),"gpu"],check=True,env=env,timeout=60)

 mutants=[('unselected acquire',code.replace('if (mask & (1 << i)) {','if (true) {',1)),('duplicate selected acquire',code.replace('blocks[i] = retro_atomic_load_acquire_ptr(&ram_list.dirty_memory[i]);','blocks[i] = retro_atomic_load_acquire_ptr(&ram_list.dirty_memory[i]); (void)retro_atomic_load_acquire_ptr(&ram_list.dirty_memory[i]);',1)),('wrong metadata consumer',code.replace('ram_list.dirty_memory[i]','ram_list.dirty_memory[0]',1)),('notification inside RCU',code.replace('    WITH_RCU_READ_LOCK_GUARD() {','    WITH_RCU_READ_LOCK_GUARD() {\n        if (xen_enabled()) xen_hvm_modified_memory(start, length);',1))]
 for name,mutant in mutants:
  assert mutant!=code,name
  (p/'test.c').write_text(mutant);subprocess.run(command,check=True,env=env)
  bad=subprocess.run([str(exe)],capture_output=True,env=env,timeout=60)
  assert bad.returncode!=0 and b'FAIL' in bad.stderr,name
  print('Required negative control rejected:',name)

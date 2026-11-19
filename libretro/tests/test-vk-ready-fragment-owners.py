#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual prepared fragment owner/selection/transport bodies; CPU only."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,subprocess,tempfile
spec=importlib.util.spec_from_file_location('ready_fragment_builder',Path(__file__).with_name('test-vk-ready-fragment.py'))
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)

def wiring(source):
 for name in ['shader_cache_entry_init','shader_cache_entry_init_view','shader_cache_entry_compare_view','shader_cache_entry_compare','pgraph_vk_ready_shader_binding','ready_binding_compare','ready_binding_init','pgraph_vk_materialize_ready_shader_binding','pgraph_vk_use_ready_shader_binding','pgraph_vk_bind_shaders']:
  assert 'ready_fragment_route' in builder.function(source,name),name
 assert 'r->ready_fragment.ready = r->ready_fragment.route = false;' in builder.function(source,'pgraph_vk_shader_objects_disable')
 cache=builder.function(source,'shader_cache_init')
 assert 'unsigned fragments = vertices ? VK_READY_FRAGMENT_STAGES : 0;' in cache
 assert 'unsigned reserved = vertices + geometry + fragments;' in cache
 init=builder.function(source,'pgraph_vk_init_shaders')
 assert init.index('vk_ready_fragment_decline(r)')<init.index('vk_ready_geometry_decline(r)')
 update=builder.function(source,'update_shader_uniforms_tracked')
 assert update.index('set_psh_texscale_values')<update.index('vk_ready_fragment_parameters(binding)')
 # General worker/adoption lookups cannot inherit a draw's prepared route.
 assert 'ready_fragment' not in builder.function(source,'get_and_ref_shader_module_for_key')

def run(root,cc,out):
 out.mkdir(parents=True,exist_ok=True)
 owned=['hw/xbox/nv2a/pgraph/vk/ready-fragment.inc','hw/xbox/nv2a/pgraph/vk/renderer.h','hw/xbox/nv2a/pgraph/vk/shaders.c']
 before={p:hashlib.sha256((root/p).read_bytes()).hexdigest()for p in owned}
 header=(root/owned[1]).read_text();source=(root/owned[2]).read_text();bank=(root/owned[0]).read_text()
 wiring(source)
 wiring_controls=[]
 for name in ['shader_cache_entry_init','shader_cache_entry_compare','pgraph_vk_ready_shader_binding','pgraph_vk_materialize_ready_shader_binding','pgraph_vk_use_ready_shader_binding']:
  original=builder.function(source,name);changed=source.replace(original,original.replace('ready_fragment_route','missing_fragment_route'))
  try:wiring(changed)
  except AssertionError:wiring_controls.append(name)
  else:raise AssertionError('namespace wiring control escaped: '+name)
 fixture=Path(__file__).with_suffix('.c').read_text()
 constants='\n'.join(re.findall(r'^#define VK_READY_FRAGMENT_\w+ .*$',header,re.M))+'\n#define PSH_UBO_BINDING 1\n#define PSH_TEX_BINDING 2\n'
 fixture=fixture.replace('/* CONSTANTS */',constants).replace('/* PRODUCTION */',bank)
 bind=builder.function(source,'pgraph_vk_bind_shaders');start=bind.index('        bool dirty =');end=bind.index('        ShaderBinding *binding = r->shader_binding;',start)
 fixture=fixture.replace('/* BIND_ROUTE */',bind[start:end])
 generated=out/'owners-source.c';generated.write_text(fixture)
 exe,env,_=builder.build(root,cc,generated,out,gpu=False)
 result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=120);(out/'run.log').write_text(result.stdout+'\nSTDERR\n'+result.stderr);print(result.stdout,end='');assert result.returncode==0,result.stdout+result.stderr
 # Retain exact source bodies and compile independent deliberate faults. The
 # expensive real compiler/generator objects are built once for these controls.
 manifest=json.loads((out/'manifest.json').read_text());command=manifest['command'];indices=[i for i,arg in enumerate(command)if arg.endswith('.c')];prefix=command[:indices[0]];tail=command[indices[-1]+1:];objects=[]
 for n,index in enumerate(indices[1:]):
  obj=out/('shared-%u.o'%n)
  compiled=subprocess.run(prefix+['-c',command[index],'-o',str(obj)],env=env,capture_output=True,text=True,timeout=120)
  assert compiled.returncode==0,compiled.stdout+compiled.stderr
  objects.append(str(obj))
 mutations=[
  ('layout-total','module->uniforms.total_size != VK_READY_FRAGMENT_TOTAL','false'),
  ('suffix-offset','+ VK_READY_FRAGMENT_OFFSET;','+ VK_READY_FRAGMENT_OFFSET - 4;'),
  ('suffix-invalidation','module->uniform_staged_valid = false;','/* omitted staged invalidation */'),
  ('all-four-views','i < NV2A_MAX_TEXTURES','i < 1'),
  ('typed-view','default: return false;','default: break;'),
  ('live-program','vk_ready_fragment_views(r) && vk_ready_fragment_live_program(pg)','vk_ready_fragment_views(r)'),
  ('bank-release','if (entry->module_info) pgraph_vk_unref_shader_module(r, entry->module_info);','/* omitted bank release */'),
  ('transition-capture','state = pgraph_glsl_get_shader_state(pg);\n                fragment_route','state = r->shader_binding->state;\n                fragment_route'),
  ('fresh-route-recheck','fragment_route = vk_ready_fragment_route(pg, &state.psh);','/* omitted fresh-owner admission recheck */'),
 ]
 controls=[]
 for name,old,new in mutations:
  assert fixture.count(old)==1,(name,fixture.count(old))
  mutant=out/(name+'.c');mutant.write_text(fixture.replace(old,new));binary=out/(name+exe.suffix)
  changed=prefix+[str(mutant)]+objects+tail;changed[changed.index('-o')+1]=str(binary)
  built=subprocess.run(changed,env=env,capture_output=True,text=True,timeout=120);(out/(name+'-build.log')).write_text(built.stdout+built.stderr);assert built.returncode==0,built.stdout+built.stderr
  failed=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=120);(out/(name+'-run.log')).write_text(failed.stdout+failed.stderr);assert failed.returncode!=0,'deliberate backend fault escaped: '+name
  controls.append(dict(name=name,returncode=failed.returncode))
 assert all(hashlib.sha256((root/p).read_bytes()).hexdigest()==v for p,v in before.items())
 (out/'owner-source-manifest.json').write_text(json.dumps(dict(source_sha256=before,generated_sha256=hashlib.sha256(generated.read_bytes()).hexdigest(),returncode=result.returncode,controls=controls,wiring_controls=wiring_controls,qualification='CPU actual owner/admission/route/helper bodies; GPU use-retirement primitive separately qualified'),indent=2))
 print('Prepared fragment backend:9 executable fault controls +5 namespace wiring controls rejected')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('compiler',nargs='?',default='cc');p.add_argument('--keep',type=Path);a=p.parse_args()
 if a.keep:run(a.root.resolve(),a.compiler,a.keep.resolve())
 else:
  with tempfile.TemporaryDirectory(prefix='vk-ready-fragment-owners-')as d:run(a.root.resolve(),a.compiler,Path(d))
if __name__=='__main__':main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-bound proof for the 60 native owners serving all 120 logical prefixes."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,sys,tempfile
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('checkout',type=Path);parser.add_argument('cc',nargs='?',default='cc')
parser.add_argument('--keep',type=Path,help='preserve generated sources, proof words and manifest, including failures')
parser.add_argument('--structural-only',action='store_true',help='CPU identity proof only; explicitly skip external spirv-val')
args=parser.parse_args();r=args.checkout.resolve();cc=args.cc;structural_only=args.structural_only
_tmp=None
if args.keep:
 out=args.keep.resolve();out.mkdir(parents=True,exist_ok=False)
else:
 _tmp=tempfile.TemporaryDirectory(prefix='vk-ready-prefix-alias-');out=Path(_tmp.name)
files=['hw/xbox/nv2a/pgraph/vk/ready-vertex.inc','hw/xbox/nv2a/pgraph/vk/renderer.h','hw/xbox/nv2a/pgraph/vk/glsl.c','hw/xbox/nv2a/pgraph/vk/constants.h','hw/xbox/nv2a/pgraph/glsl/common.c','hw/xbox/nv2a/pgraph/glsl/vsh.c','hw/xbox/nv2a/pgraph/glsl/vsh-prog.c','hw/xbox/nv2a/pgraph/glsl/vsh-interpreter.inc','hw/xbox/nv2a/pgraph/glsl/vsh-ff.c','hw/xbox/nv2a/pgraph/glsl/vsh-controls.inc','hw/xbox/nv2a/pgraph/gl/ready-ff-state.h','hw/xbox/nv2a/pgraph/spv/glslc.c','hw/xbox/nv2a/pgraph/spv/glslc.h','hw/xbox/nv2a/pgraph/spv/glslc_lex.c']
original={n:(r/n).read_bytes() for n in files};sha=lambda v:hashlib.sha256(v).hexdigest()
manifest={'source_sha256':{n:sha(v) for n,v in original.items()},'scope':'CPU/compiler identity only; native alias lifetime and GS consumer qualification are separate','prefix_uses':{n:[{'line':i,'text':line} for i,line in enumerate(v.decode().splitlines(),1) if 'prefix_outputs' in line or n.endswith('common.c') and ('prefix_s' in line or 'bool prefix' in line)] for n,v in original.items()}}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
def function(text,name):
 m=re.search(r'^(?:static\s+)?MString\s*\*'+name+r'\([^;{]*\)\s*\{',text,re.M);assert m,name
 end=text.index('{',m.start())+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]
inc=original[files[0]].decode();start=inc.index('        entry->key.kind =',inc.index('static void vk_ready_vertex_init'))
assert 'entry->module_info = r->ready_vertex.stages[i - 1].module_info;' in inc
assert 'pgraph_vk_ref_shader_module(entry->module_info); /* alias slot owns one reference */' in inc
recipe=inc[start:inc.index('        if (i & 1u) {',start)]
header=original[files[1]].decode();constants=re.search(r'enum VkReadyVertexKind \{[^;]+;',header)[0]+'\n'
for name in ['VK_READY_VERTEX_VERSION','VK_READY_VERTEX_STAGES']:constants+=re.search(r'^#define '+name+r' .+$',header,re.M)[0]+'\n'
constants+=re.search(r'^#define VSH_UBO_BINDING .+$',header,re.M)[0]+'\n'
glsl=original[files[2]].decode();at=glsl.index('ShaderModuleInfo *pgraph_vk_create_optional_vertex_module(');opts=re.search(r'GlslcOptions opts = \{.*?\};',glsl[at:],re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* ACTUAL BANK CONSTANTS */',constants).replace('/* ACTUAL BANK RECIPE */',recipe).replace('/* ACTUAL COMPILER OPTIONS */',opts)
(out/'proof.c').write_text(code)
(out/'qemu').mkdir();(out/'qemu/osdep.h').write_text('''#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define stringify1(x) #x
#define stringify(x) stringify1(x)
''')
ref='''#include "hw/xbox/nv2a/pgraph/glsl/vsh.h"
#include "hw/xbox/nv2a/pgraph/glsl/vsh-prog.h"
#include "hw/xbox/nv2a/pgraph/glsl/vsh-ff.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#define DECL_NAME(t) #t,
const char *uniform_element_type_to_str[]={UNIFORM_ELEMENT_TYPE_X(DECL_NAME)};
DEF_UNIFORM_INFO_ARR(VshUniform,VSH_UNIFORM_DECL_X)
'''
ref+='#include "'+(r/'hw/xbox/nv2a/pgraph/glsl/vsh-controls.inc').as_posix()+'"\n'
ref+=function(original['hw/xbox/nv2a/pgraph/glsl/common.c'].decode(),'pgraph_glsl_get_vtx_header')+'\n'
for n in ['pgraph_glsl_gen_vsh_impl','pgraph_glsl_gen_vsh','pgraph_glsl_gen_vsh_interpreter_dynamic']:ref+=function(original['hw/xbox/nv2a/pgraph/glsl/vsh.c'].decode(),n)+'\n'
(out/'generator.c').write_text(ref)
exe=out/('proof.exe' if os.name=='nt' else 'proof');env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
flags=[cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-Wno-missing-field-initializers','-Wno-cast-function-type','-I'+str(out),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro/glib'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/spirv-headers/include'),'-I'+str(r/'deps/vulkan-headers')]
# Windows GCC diagnoses the production FF invalid-enum fallthrough even though
# every bank recipe below is independently bounded to the seven valid modes.
# Retain the warning in compile.log; all other diagnostics remain errors.
if os.name=='nt':flags+=['-Wno-error=maybe-uninitialized']
sources=[out/'proof.c',out/'generator.c']+[r/n for n in ['hw/xbox/nv2a/pgraph/glsl/vsh-prog.c','hw/xbox/nv2a/pgraph/glsl/vsh-ff.c','hw/xbox/nv2a/pgraph/spv/glslc.c','hw/xbox/nv2a/pgraph/spv/glslc_lex.c','libretro/glib/gcore.c']]
if os.name=='nt':sources += [r/'libretro/glib/gcontainers.c']+[r/'libretro-common'/n for n in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']]
cmd=flags+list(map(str,sources))+['-Wl,--gc-sections','-lm','-o',str(exe)];manifest['compiler_command']=cmd
try:
 with (out/'compile.log').open('w') as f:
  compiled=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT)
 manifest['compile_log_sha256']=sha((out/'compile.log').read_bytes())
 manifest['windows_legacy_warning_policy']='Windows production vsh-ff.c valid-mode proof retains maybe-uninitialized warning; all other warnings remain errors' if os.name=='nt' else 'All selected compiler warnings remain errors'
 if compiled.returncode:raise RuntimeError((out/'compile.log').read_text())
 captures=out/'modules';captures.mkdir()
 with (out/'pairs.log').open('w') as f:subprocess.run([str(exe),str(captures)],env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
 assert len(list(captures.glob('*.spv')))==240
 validator=shutil.which('spirv-val',path=env['PATH'])
 if not structural_only:
  assert validator,'UNQUALIFIED: real spirv-val required'
  for module in sorted(captures.glob('*.spv')):subprocess.run([validator,'--target-env','vulkan1.3',str(module)],env=env,check=True)
 manifest['controls']=[]
 for mutation,label in [(1,'wrong location decoration'),(2,'wrong uniform offset'),(3,'wrong FP target')]:
  folder=out/f'control-{mutation}';folder.mkdir()
  result=subprocess.run([str(exe),str(folder),str(mutation)],env=env,capture_output=True,text=True);(folder/'result.log').write_text(result.stdout+result.stderr)
  assert result.returncode and re.search('assertion|Assertion',result.stderr),(label,result.returncode)
  manifest['controls'].append({'label':label,'exit_code':result.returncode,'rejected':True})
 manifest['equivalence_groups']={}
 for target in range(2):
  groups=[]
  for index in range(120):
   stem=f'env{target}-index{index:03d}';words=(captures/(stem+'.ndspv')).read_bytes();layout=(captures/(stem+'.layouts')).read_bytes()
   for group in groups:
    if words==group['words'] and layout==group['layout']:
     group['indices'].append(index);break
   else:groups.append({'words':words,'layout':layout,'indices':[index]})
  manifest['equivalence_groups'][str(target)]=[{'indices':g['indices'],'nondebug_sha256':sha(g['words']),'layouts_sha256':sha(g['layout'])} for g in groups]
 manifest['group_comparison']='Linear actual full non-debug word bytes AND complete reflected layout bytes, no hash-only equivalence; all semantic Flat/Location/FP modes retained.'
 manifest['module_sha256']={n.name:sha(n.read_bytes()) for n in captures.glob('*.spv')};manifest['qualified_cpu_identity']=True
finally:
 manifest['sources_unchanged']=all((r/n).read_bytes()==v for n,v in original.items());manifest['all_prefix_groups_exact']=len(manifest.get('equivalence_groups',{}))==2 and all(len(g)==60 and all(len(x['indices'])==2 for x in g) for g in manifest.get('equivalence_groups',{}).values())
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2));assert manifest['sources_unchanged']
assert manifest['all_prefix_groups_exact']
print('Prepared Vulkan vertex prefix identity: all120 logical states, 60 exact non-debug/layout groups per SPIR-V target, '+('CPU structural-only (SPIR-V validation not requested)' if structural_only else '240 modules spirv-val validated')+', 3 controls rejected PASS')
if _tmp:_tmp.cleanup()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual frontend option/latch and optional startup partition/admission wiring.

No GPU, binary loading or test-only production changes. Original shader/owner
corpora remain independent and unchanged. Historical absence is not a claim
that an old build implements this connected opt-in.
"""
from pathlib import Path
import os,re,shutil,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
config=(root/'hw/xbox/nv2a/pgraph/gl/ready-config.h').read_text() if (root/'hw/xbox/nv2a/pgraph/gl/ready-config.h').is_file() else ''
ui=(root/'ui/libretro.c').read_text();options=(root/'ui/libretro_core_options.h').read_text()
advertised=any(x in config for x in ['PGRAPH_GL_READY_OPTION','pgraph_gl_prepared_shaders_enabled']) or 'xemu_gl_prepared_shaders' in options or 'update_gl_prepared_shader_option' in ui
if not advertised:
 print('HISTORICAL: no connected prepared-shader option; not qualified as opt-in wiring',flush=True);raise SystemExit(0)
assert re.search(r'^#define PGRAPH_GL_READY_OPTION 1$',config,re.M),'Advertised option lacks its complete configuration contract'
assert '#include <stdbool.h>' in config and 'bool pgraph_gl_prepared_shaders_enabled(void);' in config
assert '#include "hw/xbox/nv2a/pgraph/gl/ready-config.h"' in ui

def function(text,signature):
 a=text.index(signature);b=text.index('{',a)+1;depth=1
 while depth:depth+=(text[b]=='{')-(text[b]=='}');b+=1
 return text[a:b]

update=function(ui,'static void update_variables(void)\n{');populate=function(ui,'static void populate_config(');load=function(ui,'RETRO_API bool retro_load_game(')
assert update.count('update_gl_prepared_shader_option();')==1
assert populate.count('latch_gl_prepared_shader_option();')==1
assert load.index('use_vulkan = want_vulkan;')<load.index('populate_config(opt_dvd_path);')<load.index('emu_started_once = true;')
assert 'if (emu_started_once && !emu_started_vulkan)' in load and 'restart RetroArch to load another game' in load
ui_code=ui[ui.index('/* Preparation is an opt-in startup/GPU-work tradeoff'):ui.index('static bool context_ready',ui.index('/* Preparation is an opt-in startup/GPU-work tradeoff'))]
shader=(root/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text();init=function(shader,'void pgraph_gl_init_shaders(')
predicate=re.search(r'const bool prepare = [^;]+;',init)[0]
starts=[m.start() for m in re.finditer(r'if \(prepare\) \{',init)];assert len(starts)==3
blocks=[function(init[x:],'if (prepare)') for x in starts]
assert init.index('pipeline_owner_finalize(r);')<starts[2]
# The option controls preparation, not ordinary exact native-tuple sharing.
for signature,call in [('static void generate_shaders(','pipeline_owner_acquire(r, binding)'),('static void shader_cache_entry_post_evict(','pipeline_owner_release(r, binding)')]:
 body=function(shader,signature);assert call in body and 'pgraph_gl_prepared_shaders_enabled' not in body
binding_free=function(init,'for (int i = reserved_bindings;');module_free=function(init,'for (int i = reserved_geometry;')
template=Path(__file__).with_suffix('.c').read_text()
env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='gl-prepared-option-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <stdlib.h>\n#include <string.h>\n#include <assert.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 def run(defines=(),ui_variant=ui_code,predicate_variant=predicate,blocks_variant=blocks,option_variant=options):
  (d/'ui').mkdir(exist_ok=True);(d/'ui/libretro_core_options.h').write_text(option_variant)
  code=template.replace('/* UI_OPTION */',ui_variant).replace('/* PREPARE_PREDICATE */',predicate_variant).replace('/* BINDING_RESERVATIONS */',blocks_variant[0]).replace('/* MODULE_RESERVATIONS */',blocks_variant[1]).replace('/* OPTIONAL_INIT */',blocks_variant[2]).replace('/* BINDING_FREE */',binding_free).replace('/* MODULE_FREE */',module_free)
  c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test');c.write_text(code)
  cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(root),'-I'+str(root/'libretro-common/include'),str(c),'-o',str(exe)]
  subprocess.run(cmd+['-D'+x for x in defines],env=env,check=True)
  return subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
 variants=[('ordinary',[]),('Android',['__ANDROID__=1']),('developer-optout',['XEMU_DISABLE_READY_GL=1']),('zero-valued-optout',['XEMU_DISABLE_READY_GL=0']),('explicit-reference-symbols',['XEMU_DISABLE_READY_GL=1','XEMU_EXPERIMENTAL_READY_VSH=1','XEMU_EXPERIMENTAL_READY_FF=1'])]
 for name,defines in variants:
  result=run(defines);assert result.returncode==0,(name,result.returncode,result.stdout,result.stderr);print(name+': '+result.stdout.decode(),end='',flush=True)
 faults=[
  ('live pending value leaked',dict(ui_variant=ui_code.replace('return gl_prepared_shaders.enabled;','return gl_prepared_shaders.pending;'))),
  ('latched value replaced',dict(ui_variant=ui_code.replace('!use_vulkan && !gl_prepared_shaders.latched','!use_vulkan'))),
  ('Vulkan incorrectly latches',dict(ui_variant=ui_code.replace('!use_vulkan && !gl_prepared_shaders.latched','!gl_prepared_shaders.latched'))),
  ('frontend option ignored',dict(ui_variant=ui_code.replace('!strcmp(var.value, "enabled")','(!strcmp(var.value, "enabled") && false)'))),
  ('SSO capability ignored',dict(predicate_variant=predicate.replace('gl_sso_enabled() && ',''))),
 ]
 for index,name in enumerate(['binding reservation ignores option','module reservation ignores option','native initialization ignores option']):
  altered=list(blocks);altered[index]=altered[index].replace('if (prepare)','if (true)',1);faults.append((name,dict(blocks_variant=altered)))
 start=options.index('    {\n        "xemu_gl_prepared_shaders",');end=options.index('\n    },',start)+len('\n    },');entry=options[start:end]
 changed=entry.replace('        "disabled"\n','        "enabled"\n');assert changed!=entry
 faults.append(('option enabled by default',dict(option_variant=options[:start]+changed+options[end:])))
 for name,kwargs in faults:
  result=run(**kwargs);assert result.returncode==86 and b'PREPARED_OPTION_FAIL' in result.stderr,(name,result.returncode,result.stdout,result.stderr)
  print('Required prepared-option fault rejected:',name,flush=True)
 print('PASS actual prepared option: five platform/build variants, 100 startup cases and nine runtime-negative controls',flush=True)

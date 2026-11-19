# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production query/pass/cost-model control against a delayed GPU model."""
from pathlib import Path
import os
import ctypes
import re
import subprocess
import sys
import tempfile
import shutil

repo=Path(sys.argv[1]).resolve()
if os.name=='nt':
    k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
source=(repo/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
parts=[]
for name in ('begin_query','end_query','begin_render_pass','vk_cost_close_recorded_queries',
             'end_render_pass','vk_cost_probe_active','vk_cost_materialize','clear_needs_color_draw','begin_draw','end_draw'):
    match=re.search(r'^static (?:void|bool) '+name+r'\([^;{]*\)\s*\{',source,re.M);assert match,name
    end=source.index('{',match.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    parts.append(source[match.start():end])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
has_so='shader_objects_begin_rendering' in source
if (repo/'hw/xbox/nv2a/pgraph/vk/shader-object.inc').exists():assert has_so,'current SO implementation lost its scope consumer'
header=(repo/'libretro/tests/vk-query-fixture.h').read_text()
needle='    bool host_query_reset,in_command_buffer,in_render_pass,in_draw;'
assert header.count(needle)==1
header=header.replace(needle,needle+'\n    struct {bool draw,rendering,bound;} shader_objects;')
header=header.replace('#include "../../hw/xbox/nv2a/nv2a_regs.h"','#include "'+(repo/'hw/xbox/nv2a/nv2a_regs.h').as_posix()+'"')
code=code.replace('#include "vk-query-fixture.h"',header).replace('/* CAPABILITIES */',f'#define HAVE_SHADER_OBJECT_SCOPE {int(has_so)}')
cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc)or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-query-scope-') as directory:
    folder=Path(directory);(folder/'test.c').write_text(code);exe=folder/('test.exe' if os.name=='nt' else 'test')
    command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                    '-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
                    '-I'+str(repo/'libretro/tests'),'-I'+str(repo/'deps/vulkan-headers'),str(folder/'test.c'),'-o',str(exe)]
    def run(text,negative=False):
        (folder/'test.c').write_text(text);subprocess.run(command,check=True,env=env)
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30,env=env)
        if negative:assert result.returncode and 'assert' in (result.stdout+result.stderr).lower(),'required query fault escaped without assertion'
        else:assert result.returncode==0,result.stdout+result.stderr;print(result.stdout,end='')
    run(code)
    if has_so:
        for name,before,after in [('dynamic scope ends as classic','vkCmdEndRendering(r->command_buffer);','vkCmdEndRenderPass(r->command_buffer);'),('dynamic begin omitted','if (r->shader_objects.draw && !pg->clearing)','if (false)'),('SO draw binding omitted','        shader_objects_bind_draw(pg);','        /* omitted SO draw binding */')]:
            assert code.count(before)==1,(name,code.count(before));run(code.replace(before,after),True);print('Required query scope fault rejected:',name)

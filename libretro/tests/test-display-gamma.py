#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise production palette publication and packed display LUT updates."""
import argparse
import ast
import shutil
from pathlib import Path
import os
import re
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('checkout',type=Path)
p.add_argument('--cc',default='gcc')
p.add_argument('--gpu',action='store_true')
p.add_argument('--output',type=Path)
p.add_argument('--epoxy-root',type=Path)
a=p.parse_args();r=a.checkout.resolve()
def function(path,name):
    source=(r/path).read_text()
    m=re.search(r'^(?:static )?(?:bool|void) '+name+r'\([^;]*?\)\s*\{',source,re.M)
    assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]
parts=[function('hw/xbox/nv2a/prmdio.c','prmdio_write'),function('hw/xbox/nv2a/nv2a.c','nv2a_get_dac_palette_snapshot')]
source=(r/'libretro/tests/test-display-gamma.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
env=dict(os.environ)
if Path(a.cc).is_absolute():env['PATH']=str(Path(a.cc).parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='display-gamma-') as folder:
    c=Path(folder)/'test.c';exe=Path(folder)/'test'
    c.write_text(source)
    subprocess.run([a.cc,'-O2','-std=gnu11','-DHAVE_THREADS','-Wall','-Wextra','-Werror',
                    '-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
                    '-I'+str(r),'-I'+str(r/'libretro-common/include'),str(c),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)

if a.gpu:
    with tempfile.TemporaryDirectory(prefix='display-gamma-gpu-') as temporary:
        out=a.output.resolve() if a.output else Path(temporary)
        out.mkdir(parents=True,exist_ok=True)
        header=(r/'hw/xbox/nv2a/pgraph/gamma.h').read_text()
        macro=header[header.index('#define PGRAPH_GAMMA_GLSL'):header.index('\n\n#endif')]
        def shader_text(body):
            body=body.replace('PGRAPH_GAMMA_GLSL',macro)
            return ''.join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',body))
        gl=(r/'hw/xbox/nv2a/pgraph/gl/display.c').read_text()
        start=gl.index('    const char *fs =')
        gl=shader_text(gl[start:gl.index('\n\n    ui.prog =',start)])
        vk=(r/'hw/xbox/nv2a/pgraph/vk/display.c').read_text()
        start=vk.index('static const char *display_frag_glsl =')
        vk=shader_text(vk[start:vk.index('\n\nstatic void create_descriptor_pool',start)])
        vertex='#version 450\nvoid main(){float x=-1.0+float((gl_VertexIndex&1)<<2);float y=-1.0+float((gl_VertexIndex&2)<<1);gl_Position=vec4(x,y,0,1);}'
        sources={'display-gl.frag':gl,'display-vk.frag':vk.replace('const bool byte_scanout = true;','const bool byte_scanout = false;',1),'display-byte.frag':vk,'display.vert':vertex}
        for name,text in sources.items():(out/name).write_bytes(text.encode())
        flags=['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
               '-I'+str(r),'-I'+str(r/'libretro/tests')]
        gl_exe=out/('gamma-gl.exe' if os.name=='nt' else 'gamma-gl')
        command=[a.cc,*flags,str(r/'libretro/tests/test-display-gamma-gl.c'),'-o',str(gl_exe)]
        if os.name=='nt':
            epoxy=(a.epoxy_root or r/'deps').resolve()
            command+=['-I'+str(epoxy/'libepoxy/include'),str(epoxy/'libepoxy.a'),'-lopengl32','-lgdi32','-static']
        else:command+=['-lEGL','-lGL']
        subprocess.run(command,check=True,env=env)
        subprocess.run([str(gl_exe),str(out/'display-gl.frag')],check=True,env=dict(env,EGL_PLATFORM='surfaceless'))
        helper=(r/'libretro/tests/spv-clear-vulkan.h').read_text()
        helper=helper[:helper.index('static void raster_tests')]
        helper=helper.replace('VkInstanceCreateInfo ci={',
            'VkValidationFeatureEnableEXT feature=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;'
            'VkValidationFeaturesEXT validation={.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,.enabledValidationFeatureCount=1,.pEnabledValidationFeatures=&feature};'
            'VkInstanceCreateInfo ci={.pNext=validation_enabled?&validation:NULL,',1)
        fixture=(r/'libretro/tests/test-display-gamma-vk.c').read_text().replace('/* GPU HELPERS */',helper)
        test=out/'gamma-vk.c';test.write_bytes(fixture.encode())
        exe=out/('gamma-vk.exe' if os.name=='nt' else 'gamma-vk')
        command=[a.cc,*flags,'-Wno-unused-function','-Wno-unused-variable','-Wno-sign-compare',
                 '-Wno-missing-field-initializers','-I'+str(r/'deps/volk'),'-I'+str(r/'deps/vulkan-headers'),
                 '-I'+str(r/'deps/spirv-headers/include'),str(test),
                 str(r/'hw/xbox/nv2a/pgraph/spv/glslc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),
                 str(r/'deps/volk/volk.c'),'-lm','-o',str(exe)]
        if os.name!='nt':command+=['-ldl']
        subprocess.run(command,check=True,env=env)
        subprocess.run([str(exe),str(out/'display.vert'),str(out/'display-vk.frag'),str(out/'display-byte.frag')],check=True,env=env)
        validator=shutil.which('spirv-val',path=env['PATH'])
        if validator:
            for path in out.glob('*.spv'):subprocess.run([validator,str(path)],check=True,env=env)

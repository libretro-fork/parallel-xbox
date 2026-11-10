# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check fixed scanout words/layouts and borrowed native module ownership.
--generate explicitly refreshes the checked-in words with the in-tree compiler.
Normal CI never regenerates its expected modules.
"""
from pathlib import Path
import argparse,ctypes,os,re,shutil,subprocess,tempfile

def function(source,name):
    m=re.search(r'^(?:ShaderModuleInfo \*|void)\s*'+name+r'\([^;]*?\)\s*\{',source,re.M)
    assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]

def run(r,cc,folder,generate):
    if os.name=='nt':
        kernel=ctypes.WinDLL('kernel32');kernel.GetErrorMode.restype=ctypes.c_uint
        kernel.SetErrorMode(kernel.GetErrorMode()|0x8003)
    env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
    source=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
    code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(function(source,n) for n in ['pgraph_vk_create_static_shader_module','pgraph_vk_destroy_shader_module']))
    errors=(r/'libretro/tests/test-spv-geom.c').read_text();a=errors.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=errors.index('int line_tests(',a)
    code=code.replace('/* QUIET ERRORS */',errors[a:b]);(folder/'test.c').write_text(code)
    exe=folder/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers',
        '-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),
        str(folder/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/display.c'),
        str(r/'hw/xbox/nv2a/pgraph/spv/glslc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),
        '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc','-lm','-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe),str(folder)]+(['generate'] if generate else []),check=True,env=env,timeout=120)
    validator=shutil.which('spirv-val',path=env['PATH'])
    if validator:
        for file in folder.glob('*.spv'):
            version=int.from_bytes(file.open('rb').read(8)[4:8],'little')
            subprocess.run([validator,'--target-env','vulkan1.3' if version>=0x10600 else 'vulkan1.2',str(file)],check=True,env=env)
        print('Static display: eight reference/native modules validated',flush=True)
    else:print('SKIP: spirv-val not installed',flush=True)
    if generate:
        shutil.copyfile(folder/'regenerate.inc',r/'hw/xbox/nv2a/pgraph/spv/display-words.inc')
        print('Regenerated display-words.inc; rerun without --generate to verify',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='gcc');p.add_argument('--generate',action='store_true');p.add_argument('--keep',type=Path);a=p.parse_args()
    if a.keep:a.keep.mkdir(parents=True,exist_ok=True);run(a.checkout.resolve(),a.cc,a.keep.resolve(),a.generate)
    else:
        with tempfile.TemporaryDirectory(prefix='spv-display-') as folder:run(a.checkout.resolve(),a.cc,Path(folder),a.generate)

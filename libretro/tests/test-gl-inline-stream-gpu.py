# SPDX-License-Identifier: LGPL-2.1-or-later
"""Actual upload/binding functions and GPU inputs through wraps and fallbacks.

DMA/RAM paths are modeled and forbidden here. Inline layout, pointer shadow,
format selection, provoking reads, upload owner and native mapping are actual
production functions. Feedback/readback storage is correctness-only machinery.
"""
from pathlib import Path
import importlib.util,os,re,shutil,subprocess,sys,tempfile,time
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
module=Path(__file__).with_name('test-vsh-interpreter-dynamic.py')
spec=importlib.util.spec_from_file_location('dynamic_fixture',module);fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
vertex=(root/'hw/xbox/nv2a/pgraph/gl/vertex.c').read_text(encoding='utf-8')
common=(root/'hw/xbox/nv2a/pgraph/vertex.c').read_text(encoding='utf-8')
header=(root/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text(encoding='utf-8')
def function(source,name):
    match=re.search(r'^(?:static )?(?:void|bool|unsigned int) '+name+r'\([^;]*?\)\s*\{',source,re.M);assert match,name
    end=source.index('{',match.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]
production=function(common,'pgraph_update_inline_value')+'\n'+'\n'.join(function(vertex,n) for n in ['pgraph_gl_attr_pointer','pgraph_gl_attr_enable','pgraph_gl_attr_value','attr_gl_format','pgraph_gl_bind_vertex_attributes','init_inline_stream','finalize_inline_stream','pgraph_gl_bind_inline_array'])
fields=re.search(r'    struct \{\s+GLuint buffer;\s+uint8_t \*mapping;.*?\} inline_stream;',header,re.S)[0]
shadow=re.search(r'    struct AttrShadow \{.*?\} attr_shadow\[NV2A_VERTEXSHADER_ATTRIBUTES\];',header,re.S)[0]
with tempfile.TemporaryDirectory(prefix='gl-stream-gpu-') as temp:
    folder=Path(temp);fixture.prepare(root,folder,'gl')
    code=(folder/'driver.c').read_text(encoding='utf-8');code=code[:code.index('int main(')]
    for label in ['varyings','prefixed_varyings']:
        start=code.index('const char *'+label+'[]={');end=code.index(';',start)+1
        code=code[:start]+'const char *'+label+'[]={"capture_value","stamp"};'+code[end:]
    code+=Path(__file__).with_suffix('.c').read_text(encoding='utf-8').replace('/* FIELDS */',fields+'\n'+shadow).replace('/* PRODUCTION */',production)
    script=(folder/'driver.py').read_text(encoding='utf-8')
    script=script[:script.index(' # Keep the full growing corpus')]+'\n subprocess.run([str(exe)],env=os.environ,check=True,timeout=180)\n'
    script=script.replace("'-O2','-std=gnu11'","'-O2','-std=gnu11','-fno-strict-aliasing','-DXEMU_EXPERIMENTAL_INLINE_STREAM'")
    (folder/'driver.py').write_text(script,encoding='utf-8')
    env=dict(os.environ,EGL_PLATFORM='surfaceless');env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
    deadline=time.monotonic()+300
    for fault in [None,'offset','copy']:
        text=code
        if fault=='offset':
            before='attrib_data_addr += r->inline_stream.draw_offset;';assert text.count(before)==1;text=text.replace(before,'attrib_data_addr += 0;',1)
        if fault=='copy':
            before='pgraph_inline_array_data(pg), buffer_size);';assert text.count(before)==1;text=text.replace(before,'pgraph_inline_array_data(pg), 0);',1)
        (folder/'driver.c').write_text(text,encoding='utf-8')
        result=subprocess.run([sys.executable,str(folder/'driver.py'),str(root),cc],env=env,capture_output=True,timeout=max(0,deadline-time.monotonic()))
        if fault:
            assert result.returncode!=0 and b'STREAM_GPU_MISMATCH' in result.stderr,(fault,result)
            print('Required GPU upload fault rejected:',fault,flush=True)
        else:
            assert result.returncode==0,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='',flush=True)

# SPDX-License-Identifier: LGPL-2.1-or-later
"""Production mapped-owner setup, upload publication, fallback and teardown."""
from pathlib import Path
import os,re,subprocess,sys,tempfile,time
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(root/'hw/xbox/nv2a/pgraph/gl/vertex.c').read_text(encoding='utf-8')
def function(name):
    match=re.search(r'^(?:static )?(?:void|unsigned int) '+name+r'\([^;]*?\)\s*\{',source,re.M);assert match,name
    end=source.index('{',match.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]
production='\n'.join(function(n) for n in ['init_inline_stream','finalize_inline_stream','pgraph_gl_bind_inline_array'])
header=(root/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text(encoding='utf-8')
fields=re.search(r'    struct \{\s+GLuint buffer;\s+uint8_t \*mapping;.*?\} inline_stream;',header,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text(encoding='utf-8').replace('/* FIELDS */',fields).replace('/* PRODUCTION */',production)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='gl-stream-owner-') as tmp:
    deadline=time.monotonic()+300
    def remaining():
        value=deadline-time.monotonic();assert value>0,'shared stream-owner watchdog expired';return value
    d=Path(tmp);src=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
    command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-DXEMU_EXPERIMENTAL_INLINE_STREAM','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(root),str(src),'-o',str(exe)]
    def run(text):
        src.write_text(text,encoding='utf-8');subprocess.run(command,env=env,check=True,timeout=remaining())
        return subprocess.run([str(exe)],env=env,capture_output=True,timeout=remaining())
    result=run(code);assert result.returncode==0,result.stderr;print(result.stdout.decode(),end='')
    for name,before,after in [
        ('mapping not coherent','GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT','GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT'),
        ('wrong final storage offset','r->inline_stream.mapping + stream_offset,','r->inline_stream.mapping + stream_offset + 16,'),
        ('wrong published native owner','r->inline_stream.draw_buffer = r->inline_stream.buffer;','r->inline_stream.draw_buffer = r->gl_inline_array_buffer;'),
        ('partial mapping leak','            glUnmapBuffer(GL_ARRAY_BUFFER);','            (void)glUnmapBuffer;'),
        ('deleted binding resurrected','(GLuint)previous == buffer ? 0 : previous','previous'),
        ('owner not touched before gameplay','memset(mapping, 0, GL_INLINE_STREAM_BYTES);','(void)mapping;'),
    ]:
        assert code.count(before)==1,name
        result=run(code.replace(before,after,1));assert result.returncode==1 and b'FAIL stream owner' in result.stderr,(name,result)
        print('Required stream-owner fault rejected:',name)

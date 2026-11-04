#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual inline packet, ownership and upload reservation helpers."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
def function(source,name):
    m=re.search(r'^(?:static )?(?:inline )?(?:uint32_t \*|void |bool |int |VkDeviceSize )'+name+r'\(',source,re.M);assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]
h=(r/'hw/xbox/nv2a/pgraph/pgraph.h').read_text();v=(r/'hw/xbox/nv2a/pgraph/vertex.c').read_text();p=(r/'hw/xbox/nv2a/pgraph/pgraph.c').read_text();vk=(r/'hw/xbox/nv2a/pgraph/vk/vertex.c').read_text();b=(r/'hw/xbox/nv2a/pgraph/vk/buffer.c').read_text();n=(r/'hw/xbox/nv2a/nv2a.c').read_text();draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();renderer=(r/'hw/xbox/nv2a/pgraph/vk/renderer.c').read_text()
a=p.index('DEF_METHOD(NV097, INLINE_ARRAY)');end=p.index('{',a)+1;depth=1
while depth:depth+=(p[end]=='{')-(p[end]=='}');end+=1
method=p[a:end].replace('DEF_METHOD(NV097, INLINE_ARRAY)','static void method_INLINE_ARRAY(NV2AState *d,PGRAPHState *pg,unsigned subchannel,unsigned method,uint32_t parameter,uint32_t *parameters,size_t num_words_available,size_t *num_words_consumed,bool inc)')
parts=[function(h,'pgraph_inline_array_data'),function(b,'pgraph_vk_buffer_has_space_for')]+[function(vk,x) for x in ['pgraph_vk_inline_array_begin','pgraph_vk_commit_inline_array']]+[function(v,x) for x in ['pgraph_materialize_inline_array','pgraph_append_inline_array','pgraph_reset_inline_buffers']]+[function(b,'pgraph_vk_append_to_buffer'),function(n,'nv2a_pre_load'),method]
finish=function(draw,'pgraph_vk_finish');assert finish.index('pgraph_materialize_inline_array(')<finish.index('if (r->in_command_buffer)')
assert 'pgraph_materialize_inline_array(&d->pgraph);' in renderer
assert not re.search(r'VMSTATE[^\n]*inline_array_external',n)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='inline-upload-writer-') as tmp:
    path=Path(tmp)/'test.c';path.write_text(code);exe=Path(os.environ.get('INLINE_WRITER_BINARY',str(Path(tmp)/'test')))
    flags=shlex.split(os.environ.get('INLINE_WRITER_CFLAGS',''))
    subprocess.run(cc+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare',*flags,str(path),'-o',str(exe)],check=True)
    if not os.environ.get('INLINE_WRITER_SKIP_RUN'):subprocess.run([str(exe)],check=True,timeout=30)

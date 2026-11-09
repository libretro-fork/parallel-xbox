# SPDX-License-Identifier: GPL-2.0-or-later
"""Force module-handle reuse and exercise native module IDs and both GPL keys."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();glsl=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
def fn(source,name):
    m=re.search(r'^(?:static )?(?:uint64_t|VkPipeline|VkPrimitiveTopology|ShaderModuleInfo \*) *'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
types=re.search(r'typedef struct RenderPassState \{.*?\} RenderPassState;',h,re.S)[0]
types+='\n'+'\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',draw,re.S)[0] for n in ('GplPreKey','GplFragKey'))
constructor=fn(glsl,'pgraph_vk_create_shader_module_from_spirv')
parts='\n'.join(fn(draw,n) for n in ('topology_class','gpl_pre_hash','gpl_frag_hash','gpl_get_pre','gpl_get_frag'))+'\n'+constructor
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */',parts).replace('/* REFERENCE */',Path(__file__).with_name('library-handle-key-reference.h').read_text())
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='library-module-identity-') as d:
    p=Path(d);src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
    sanitizer=['-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if os.name=='nt' else ['-fsanitize=undefined','-fno-sanitize-recover=all']
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*sanitizer,'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'include'),str(src),str(r/'util/fast-hash.c'),str(r/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)]
    src.write_text(code);subprocess.run(cmd,env=env,check=True);subprocess.run([str(exe)],env=env,check=True,timeout=30)
    controls=[
        ('identity reused',code.replace('info->identity = ++r->next_shader_module_identity;','info->identity = 1;',1)),
        ('vertex identity omitted',code.replace('k->vsh = vsh;','k->vsh = 0; (void)vsh;',1)),
        ('geometry identity omitted',code.replace('k->geom = geom;','k->geom = 0; (void)geom;',1)),
        ('fragment identity omitted',code.replace('k->psh = psh;','k->psh = 0; (void)psh;',1)),
        ('upper identity bits discarded',code.replace('k->vsh = vsh;','k->vsh = (uint32_t)vsh;',1)),
        ('draw library lookup uses raw vertex handle',code.replace('r, vsh->identity, geom ? geom->identity : 0,','r, (uint64_t)(uintptr_t)vsh->module, geom ? geom->identity : 0,',1)),
        ('draw library lookup uses raw geometry handle',code.replace('geom ? geom->identity : 0,','geom ? (uint64_t)(uintptr_t)geom->module : 0,',1)),
        ('draw library lookup uses raw fragment handle',code.replace('gpl_frag_hash(r, psh->identity,','gpl_frag_hash(r, (uint64_t)(uintptr_t)psh->module,',1)),
    ]
    for name,mutant in controls:
        assert mutant!=code,name;src.write_text(mutant);subprocess.run(cmd,env=env,check=True)
        bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
        assert bad.returncode==1 and b'FAIL ' in bad.stderr,name
        print('Required negative control rejected:',name)
    if os.name!='nt':
        # Linux's nonnull sanitizer diagnoses the formerly undefined empty
        # NULL span. MinGW's libc annotations do not provide this observer.
        for field,pointer in [('nb','pVertexBindingDescriptions'),('na','pVertexAttributeDescriptions')]:
            pattern=r'if \(k->'+field+r'\) \{\s*(memcpy\([^;]+;)\s*\}'
            match=re.search(pattern,code);assert match
            mutant=code[:match.start()]+match[1]+code[match.end():]
            src.write_text(mutant);subprocess.run(cmd,env=env,check=True)
            bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
            assert bad.returncode==1 and b'null pointer passed as argument 2' in bad.stderr,pointer
            print('Required sanitizer control rejected: empty NULL',pointer)

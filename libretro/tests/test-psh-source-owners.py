#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual fragment source owner lifetime and parent byte-equivalence.

The historical parent is measured as leaking one clip owner per source, then
that known leak is reclaimed by the test only. Candidate owners are never
reclaimed to manufacture a pass. All payloads use real MString and GString.
"""
from pathlib import Path
import argparse, hashlib, os, shutil, subprocess, tempfile

BASE = '396d8f1f0a34c9e8a0ea96ab988c576aba891338'
BASE_SHA256 = 'c6d2cbeaad37f0db08dab1d5b5b37987a06b66908d6f99ca7a2c069d9579763b'

def function(source, signature):
    start=source.index(signature);p=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[p]=='{')-(source[p]=='}');p+=1
    return source[start:p]

def run(root, cc, directory, baseline, mutation):
    (directory/'qemu').mkdir()
    (directory/'qemu/osdep.h').write_text("""#include <stdint.h>
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
""")
    shim="""#include "qemu/osdep.h"
#include "glib.h"
void *mstring_test_alloc(gsize);
void mstring_test_free(void *);
void mstring_test_access(MString *);
#define g_malloc mstring_test_alloc
#define g_free mstring_test_free
#define mstring_get_str original_mstring_get_str
#include "HEADER"
#undef mstring_get_str
#undef g_malloc
#undef g_free
static inline const gchar *mstring_get_str(MString *m){mstring_test_access(m);return original_mstring_get_str(m);}
"""
    # MString is an anonymous typedef: declare the access hook only after the
    # actual header establishes the type, not with an incompatible forward type.
    shim=shim.replace('void mstring_test_access(MString *);\n','')
    shim=shim.replace('static inline const gchar *','void mstring_test_access(MString *);\nstatic inline const gchar *')
    shim=shim.replace('HEADER',(root/'include/qemu/mstring.h').as_posix())
    (directory/'qemu/mstring.h').write_text(shim)
    (directory/'owners.c').write_text("""#include "qemu/mstring.h"
static struct {MString *p;unsigned group;} owners[256];
static unsigned group;
static size_t norm_formats[2],created[2];
size_t mstring_test_created(unsigned g){return created[g];}
size_t mstring_test_norm_formats(unsigned g){return norm_formats[g];}
gchar *mstring_test_strdup_printf(const gchar *fmt,...){if(!strcmp(fmt,"norm%d"))norm_formats[group]++;va_list ap;va_start(ap,fmt);gchar *p=g_strdup_vprintf(fmt,ap);va_end(ap);return p;}
void mstring_test_group(unsigned g){assert(g<2);group=g;}
void *mstring_test_alloc(gsize size){assert(size==sizeof(MString));MString *p=g_malloc(size);unsigned i=0;while(i<256&&owners[i].p)i++;assert(i<256);owners[i].p=p;owners[i].group=group;created[group]++;return p;}
void mstring_test_free(void *p){unsigned i=0;while(i<256&&owners[i].p!=p)i++;assert(i<256);owners[i].p=NULL;g_free(p);}
void mstring_test_access(MString *p){for(unsigned i=0;i<256;i++)if(owners[i].p==p)return;fprintf(stderr,"source-owner use after release\\n");exit(2);}
size_t mstring_test_live(unsigned g){size_t n=0;for(unsigned i=0;i<256;i++)n+=owners[i].p&&owners[i].group==g;return n;}
size_t mstring_test_retained_bytes(unsigned g){size_t n=0;for(unsigned i=0;i<256;i++)if(owners[i].p&&owners[i].group==g){MString *p=owners[i].p;n+=sizeof(*p)+sizeof(*p->gstr)+p->gstr->allocated_len;}return n;}
void mstring_test_cleanup(unsigned g){for(unsigned i=0;i<256;i++)if(owners[i].p&&owners[i].group==g){assert(owners[i].p->refcnt==1);mstring_unref(owners[i].p);}}
""")
    prefix='#include "hw/xbox/nv2a/pgraph/glsl/psh.h"\n#include "hw/xbox/nv2a/nv2a_regs.h"\n#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"\n#define NV2A_UNIMPLEMENTED(...) ((void)0)\ngchar *mstring_test_strdup_printf(const gchar *,...);\n#define g_strdup_printf mstring_test_strdup_printf\n'
    source=(root/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text(encoding='utf-8')
    body=source[source.index('struct InputInfo {'):source.index('void pgraph_glsl_set_psh_uniform_values(')]
    if mutation=='missing_release':
        assert body.count('mstring_unref(clip);')==1
        body=body.replace('mstring_unref(clip);','(void)clip;')
    if mutation=='prefix_clone':
        needle='MString *final = preflight;'
        assert body.count(needle)==1
        body=body.replace(needle,'MString *final=mstring_from_str(mstring_get_str(preflight));\n    mstring_unref(preflight);')
    if mutation=='norm_format_alloc':
        needle='        const char *tex_remap = ps->state->rect_tex[i] ? psh_norm_names[i] : "";'
        assert body.count(needle)==1
        body=body.replace(needle,'        g_autofree gchar *temporary_norm=g_strdup_printf("norm%d",i);\n        const char *tex_remap=ps->state->rect_tex[i]?temporary_norm:"";')
    if mutation=='premature_release':
        assert body.count('mstring_unref(clip);')==1
        body=body.replace('mstring_unref(clip);','(void)clip;')
        needle='mstring_append(final, mstring_get_str(clip));'
        assert body.count(needle)==1
        body=body.replace(needle,'mstring_unref(clip);\n    '+needle)
    info='#define DECL_NAME(t) #t,\nconst char *uniform_element_type_to_str[]={UNIFORM_ELEMENT_TYPE_X(DECL_NAME)};\nDEF_UNIFORM_INFO_ARR(PshUniform,PSH_UNIFORM_DECL_X)\n'
    header=function((root/'hw/xbox/nv2a/pgraph/glsl/common.c').read_text(encoding='utf-8'),'MString *pgraph_glsl_get_vtx_header(')
    (directory/'candidate.c').write_text(prefix+info+header+'\n'+body)
    body=baseline[baseline.index('struct InputInfo {'):baseline.index('void pgraph_glsl_set_psh_uniform_values(')]
    assert 'mstring_unref(clip);' not in body,'historical leak reference changed'
    body=body.replace('MString *pgraph_glsl_gen_psh(', 'MString *baseline_gen_psh(',1)
    (directory/'baseline.c').write_text('#define PshUniformInfo BaselinePshUniformInfo\n'+prefix+'DEF_UNIFORM_INFO_ARR(PshUniform,PSH_UNIFORM_DECL_X)\n'+body)
    executable=shutil.which(cc) or cc
    env=dict(os.environ);env['PATH']=str(Path(executable).resolve().parent)+os.pathsep+env.get('PATH','')
    exe=directory/('test.exe' if os.name=='nt' else 'test')
    flags=[cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Wno-unused-parameter','-Wno-sign-compare',
           '-I'+str(directory),'-I'+str(root),'-I'+str(root/'include'),'-I'+str(root/'libretro/glib'),'-I'+str(root/'libretro-common/include')]
    if os.environ.get('SHADER_OWNER_SANITIZE'):flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    sources=[Path(__file__).with_suffix('.c'),directory/'candidate.c',directory/'baseline.c',directory/'owners.c',root/'libretro/glib/gcore.c']
    if os.name=='nt':
        sources += [root/'libretro/glib/gcontainers.c']+[root/'libretro-common'/f for f in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']]
    else:sources += [root/'libretro-common/compat/compat_strl.c']
    subprocess.run(flags+list(map(str,sources))+['-Wl,--gc-sections','-lm','-o',str(exe)],env=env,check=True)
    result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=180)
    (directory/'result.log').write_text(result.stdout+result.stderr)
    if mutation:
        wanted={'missing_release':'mstring_test_live(1)==1','premature_release':'source-owner use after release','norm_format_alloc':'mstring_test_norm_formats(1)==0','prefix_clone':'candidate_created+1==parent_created'}[mutation]
        assert result.returncode!=0 and wanted in result.stdout+result.stderr,(mutation,result.returncode,result.stdout,result.stderr)
        print('Required fragment-owner control rejected:',mutation,flush=True)
    else:
        print(result.stdout,end='',flush=True)
        assert result.returncode==0,(result.returncode,result.stderr)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('root',type=Path);parser.add_argument('cc',nargs='?',default='cc');parser.add_argument('--reference',type=Path);parser.add_argument('--keep',type=Path)
    args=parser.parse_args();root=args.root.resolve()
    blob=args.reference.read_bytes() if args.reference else subprocess.check_output(['git','-C',str(root),'show',BASE+':hw/xbox/nv2a/pgraph/glsl/psh.c'])
    assert hashlib.sha256(blob).hexdigest()==BASE_SHA256,'Unqualified: wrong historical source fixture'
    baseline=blob.decode('utf-8')
    with tempfile.TemporaryDirectory(prefix='psh-source-owners-') as temporary:
        directory=args.keep.resolve() if args.keep else Path(temporary);directory.mkdir(parents=True,exist_ok=True)
        for mutation in ['', 'missing_release', 'premature_release', 'norm_format_alloc', 'prefix_clone']:
            target=directory/(mutation or 'positive');target.mkdir()
            run(root,args.cc,target,baseline,mutation)
if __name__=='__main__':main()

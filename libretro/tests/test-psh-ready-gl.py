#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native GL color/depth/discard comparison for experimental ready fragments.

Readbacks, reference compilation and separate output owners are test-only
correctness work, never gameplay timing or a renderer zero-copy claim.
"""
from pathlib import Path
import argparse, os, shutil, subprocess, tempfile

def function(source, signature):
    start=source.index(signature);i=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[i]=='{')-(source[i]=='}');i+=1
    return source[start:i]

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('root',type=Path);ap.add_argument('cc',nargs='?',default='cc');ap.add_argument('--keep',type=Path);args=ap.parse_args();r=args.root.resolve()
    with tempfile.TemporaryDirectory(prefix='ready-fragment-gl-') as tmp:
        d=args.keep.resolve() if args.keep else Path(tmp);d.mkdir(exist_ok=True,parents=True);(d/'qemu').mkdir()
        (d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <stdarg.h>\n#include <assert.h>\n#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))\n#define stringify1(x) #x\n#define stringify(x) stringify1(x)\n')
        pre='#include "hw/xbox/nv2a/pgraph/glsl/psh.h"\n#include "hw/xbox/nv2a/nv2a_regs.h"\n#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"\n#define NV2A_UNIMPLEMENTED(...) ((void)0)\n#define DECL_NAME(t) #t,\nconst char *uniform_element_type_to_str[]={UNIFORM_ELEMENT_TYPE_X(DECL_NAME)};\nDEF_UNIFORM_INFO_ARR(PshUniform,PSH_UNIFORM_DECL_X)\n'
        source=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text(encoding='utf-8')
        body=source[source.index('struct InputInfo {'):source.index('void pgraph_glsl_set_psh_uniform_values(')]
        header=function((r/'hw/xbox/nv2a/pgraph/glsl/common.c').read_text(encoding='utf-8'),'MString *pgraph_glsl_get_vtx_header(')
        generator=pre+header+'\n'+body
        geometry=Path(__file__).with_name('test-geom-dynamic-gl.c').read_text(encoding='utf-8')
        context=(function(geometry,'static void quiet_abort(')+'\n'+function(geometry,'static void init_gl(')).replace('Geometry GL device=', 'Fragment GL device=')
        driver=Path(__file__).with_suffix('.c').read_text(encoding='utf-8').replace('/* CONTEXT */',context)
        env=dict(os.environ,EGL_PLATFORM='surfaceless',LP_NUM_THREADS='1');env['PATH']=str(Path(shutil.which(args.cc) or args.cc).resolve().parent)+os.pathsep+env.get('PATH','')
        for mutation in ['', 'depth', 'mode', 'target', 'volume', 'clip', 'alpha', 'combiner', 'helper', 'zero', 'final', 'constant_c0', 'constant_c1', 'constant_final']:
            code=driver
            emitted=generator
            if mutation=='zero':
                emitted=emitted.replace('a==0u||a==6u','a==0u||a==1u||a==6u').replace('b==0u||b==6u','b==0u||b==1u||b==6u')
                assert emitted!=generator
            if mutation=='final':
                emitted=emitted.replace('combFinal0!=0x130e0300u','combFinal0!=0x130e0300u&&combFinal0!=0x130f0300u').replace('(combFinal1&~128u)!=0x1c00u','(combFinal1&0xff7fu)!=0x1c00u')
                assert emitted!=generator
            if mutation.startswith('constant_'):
                marker='        {"return cregs[reg];", "return cbget(reg);"},'
                assert emitted.count(marker)==1
                faults={
                    'constant_c0':'        {"(combCtl & 0x1000u) != 0u", "false"},',
                    'constant_c1':'        {"(combCtl & 0x10000u) != 0u", "false"},',
                    'constant_final':'        {"stage == 8 ||", "false ||"},\n        {"stage == 8 ||", "false ||"},',
                }
                emitted=emitted.replace(marker,faults[mutation]+'\n'+marker)
            (d/'generator.c').write_text(emitted,encoding='utf-8')
            change={'depth':'controls[0]^=1;','mode':'controls[1]=0;','target':'for(unsigned j=4;j<8;j++)controls[j]&=~2u;','volume':'for(unsigned j=4;j<8;j++)controls[j]&=~4u;','clip':'controls[0]^=32;','alpha':'glProgramUniform1ui(p,glGetUniformLocation(p,"alphaKillMask"),0);','combiner':'glProgramUniform1ui(p,glGetUniformLocation(p,"combCtl"),s->combiner_control?s->combiner_control-1:0);'}.get(mutation,'')
            code=code.replace('/* MUTATE */',change)
            if mutation=='helper':
                code=code.replace('MString *text=pgraph_glsl_gen_psh(&s,',
                    'PshState legacy=s;legacy.depth_format&=~DEPTH_FORMAT_HELPER_LIVENESS;MString *text=pgraph_glsl_gen_psh(&legacy,')
            c=d/((mutation or 'positive')+'.c');c.write_text(code,encoding='utf-8');exe=c.with_suffix('.exe' if os.name=='nt' else '')
            flags=[args.cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Wno-unused-parameter','-Wno-sign-compare','-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro/glib'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen')]
            sources=[c,d/'generator.c',r/'libretro/glib/gcore.c']
            if os.name=='nt':
                flags+=['-Wno-cast-function-type'];sources += [r/p for p in ['ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c','libretro/glib/gcontainers.c']]
                sources += [r/'libretro-common'/p for p in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']]
                libs=['-lopengl32','-lgdi32','-luser32']
            else:sources += [r/'libretro-common/compat/compat_strl.c'];libs=['-lEGL','-lGL']
            subprocess.run(flags+list(map(str,sources))+['-Wl,--gc-sections','-lm',*libs,'-o',str(exe)],check=True,env=env)
            with c.with_suffix('.log').open('w') as out: result=subprocess.run([str(exe)]+(['--constants-only'] if mutation.startswith('constant_') else []),env=env,stdout=out,stderr=subprocess.STDOUT,timeout=300)
            log=c.with_suffix('.log').read_text(errors='replace')
            if not mutation:
                print(log,end='',flush=True);assert result.returncode==0,(result.returncode,log)
            else:
                wanted='constant register mismatch' if mutation.startswith('constant_') else 'fragment difference case='
                assert result.returncode!=0 and wanted in log,(mutation,result.returncode,log)
                print('Required ready-fragment control rejected:',mutation,flush=True)
if __name__=='__main__':main()

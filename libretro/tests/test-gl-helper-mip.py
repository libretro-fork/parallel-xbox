#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Procedural native GL regression for a clipped-helper mip-selection corner.

The actual ordinary and prepared generators are checked against an independent
texture-only oracle. A CPU plane/gradient proof fixes the expected mip and
covered/rejected masks. No game assets or external fixtures are needed.
"""
from pathlib import Path
import argparse, os, re, shutil, subprocess, sys, tempfile

def function(source, signature):
    start=source.index(signature);i=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[i]=='{')-(source[i]=='}');i+=1
    return source[start:i]

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('root',type=Path);ap.add_argument('cc',nargs='?',default='cc');ap.add_argument('--keep',type=Path);args=ap.parse_args();r=args.root.resolve()
    # Missing fixtures on an advertised fixed revision remain a hard error.
    if 'PGRAPH_GLSL_GL_DEFAULT_HELPERS' in (r/'hw/xbox/nv2a/pgraph/glsl/psh.h').read_text():
        assert (r/'libretro/tests/test-gl-helper-mip.c').is_file(), 'Advertised helper fix has no GPU fixture'

    with tempfile.TemporaryDirectory(prefix='ready-fragment-gl-') as tmp:
        d=args.keep.resolve() if args.keep else Path(tmp);d.mkdir(exist_ok=True,parents=True);(d/'qemu').mkdir()
        (d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <stdarg.h>\n#include <assert.h>\n#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))\n#define stringify1(x) #x\n#define stringify(x) stringify1(x)\n')
        pre='#include "hw/xbox/nv2a/pgraph/glsl/psh.h"\n#include "hw/xbox/nv2a/nv2a_regs.h"\n#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"\n#define NV2A_UNIMPLEMENTED(...) ((void)0)\n#define DECL_NAME(t) #t,\nconst char *uniform_element_type_to_str[]={UNIFORM_ELEMENT_TYPE_X(DECL_NAME)};\nDEF_UNIFORM_INFO_ARR(PshUniform,PSH_UNIFORM_DECL_X)\n'
        source=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text(encoding='utf-8')
        body=source[source.index('struct InputInfo {'):source.index('void pgraph_glsl_set_psh_uniform_values(')]
        header=function((r/'hw/xbox/nv2a/pgraph/glsl/common.c').read_text(encoding='utf-8'),'MString *pgraph_glsl_get_vtx_header(')
        generator=pre+header+'\n'+body
        generator=generator.replace('#include "hw/xbox/nv2a/pgraph/glsl/psh-ready.inc"',(r/'hw/xbox/nv2a/pgraph/glsl/psh-ready.inc').read_text(encoding='utf-8'))
        geometry=(r/'libretro/tests/test-geom-dynamic-gl.c').read_text(encoding='utf-8')
        context=(function(geometry,'static void quiet_abort(')+'\n'+function(geometry,'static void init_gl(')).replace('Geometry GL device=', 'Fragment GL device=')
        driver=Path(__file__).with_suffix('.c').read_text(encoding='utf-8').replace('/* CONTEXT */',context)
        original=(r/'libretro/tests/test-psh-ready-gl.c').read_text()
        driver=driver.replace('/* PROGRAM */',function(original,'static GLuint program(')).replace('/* UNIFORMS */',function(original,'static void uniforms(')).replace('/* STATES */',function(original,'static PshState state_for('))

        env=dict(os.environ,EGL_PLATFORM='surfaceless',LP_NUM_THREADS='1');env['PATH']=str(Path(shutil.which(args.cc) or args.cc).resolve().parent)+os.pathsep+env.get('PATH','')
        code=driver
        (d/'generator.c').write_text(generator,encoding='utf-8')
        c=d/'positive.c';c.write_text(code,encoding='utf-8');exe=c.with_suffix('.exe' if os.name=='nt' else '')
        flags=[args.cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Wno-unused-parameter','-Wno-sign-compare','-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro/glib'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen')]
        sources=[c,d/'generator.c',r/'libretro/glib/gcore.c']
        if os.name=='nt':
            flags+=['-Wno-cast-function-type'];sources += [r/p for p in ['ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c','libretro/glib/gcontainers.c']]
            sources += [r/'libretro-common'/p for p in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']]
            libs=['-lopengl32','-lgdi32','-luser32']
        else:sources += [r/'libretro-common/compat/compat_strl.c'];libs=['-lEGL','-lGL']
        subprocess.run(flags+list(map(str,sources))+['-Wl,--gc-sections','-lm',*libs,'-o',str(exe)],check=True,env=env)
        good=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=300)
        print(good.stdout,end='',flush=True);assert good.returncode==0,(good.returncode,good.stdout,good.stderr)
        expected=28 if 'PGRAPH_GLSL_GL_DEFAULT_HELPERS' in (r/'hw/xbox/nv2a/pgraph/glsl/psh.h').read_text() else 24
        assert f'PASS helper mip corner: {expected} exact scalar checks' in good.stdout
        assert 'PASS analytic mip oracle:' in good.stdout
        if expected==28:assert 'PASS default helper source: 1308 identical ordinary default/keyed sources' in good.stdout
        for fault in range(1,5):
            bad=subprocess.run([str(exe),str(fault)],env=env,capture_output=True,text=True,timeout=300)
            assert bad.returncode==1 and re.search(r'MIP_ORACLE_FAIL path=\d+ masked=[01] actual=',bad.stderr),(fault,bad.returncode,bad.stdout,bad.stderr)
            print('Required helper-mip fault rejected:',fault,flush=True)
if __name__=='__main__':main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native GL color/depth/discard comparison for experimental ready fragments.

Readbacks, reference compilation and separate output owners are test-only
correctness work, never gameplay timing or a renderer zero-copy claim.
"""
from pathlib import Path
import argparse, os, re, shutil, subprocess, sys, tempfile, time

def function(source, signature):
    start=source.index(signature);i=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[i]=='{')-(source[i]=='}');i+=1
    return source[start:i]

def run_positive(exe, env, log_path):
    """Retain the complete corpus and one original watchdog across contexts."""
    cpus=len(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else (os.cpu_count() or 1)
    workers=min(4,cpus)
    ranges=[(1308*i//workers,1308*(i+1)//workers) for i in range(workers)]
    children=[];deadline=time.monotonic()+300
    try:
        for index,(first,limit) in enumerate(ranges):
            path=log_path.with_name(log_path.stem+'-'+str(index)+log_path.suffix)
            stream=path.open('w')
            try:child=subprocess.Popen([str(exe),'--range',str(first),str(limit)],env=env,stdout=stream,stderr=subprocess.STDOUT)
            except BaseException:stream.close();raise
            children.append((child,stream,path))
        for child,stream,path in children:
            code=child.wait(timeout=max(0,deadline-time.monotonic()))
            if code:raise subprocess.CalledProcessError(code,child.args)
    finally:
        for child,stream,path in children:
            if child.poll() is None:child.kill()
        for child,stream,path in children:
            child.wait();stream.close()
        with log_path.open('w') as out:
            out.write(f'Positive corpus: {workers} independent contexts, 1308 cases, shared 300s deadline\n')
            for child,stream,path in children:out.write(path.read_text(errors='replace'))
    total=written=discarded=0
    for index,(_,_,path) in enumerate(children):
        log=path.read_text(errors='replace')
        matches=re.findall(r'PASS ready fragment: range=(\d+):(\d+) cases=(\d+) checked=(\d+) written=(\d+) discarded=(\d+),',log)
        assert len(matches)==1,(path,log)
        first,limit,cases,checked,wrote,dropped=map(int,matches[0])
        assert (first,limit)==ranges[index] and cases==limit-first and checked==cases*320,(path,matches)
        total+=checked;written+=wrote;discarded+=dropped
    assert total==418560 and written>0 and discarded>0,(total,written,discarded)
    log=children[0][2].read_text(errors='replace')
    for marker in ['PASS independent half-plane/window/volume oracle:']:
        assert marker in log,(marker,log)
    print(f'PASS full ready-fragment corpus: 1308 cases, {total} exact checks; {workers} independent contexts, one 300s deadline',flush=True)

def run_negative(exe, env, log_path):
    """Find a real numerical witness under the unchanged shared watchdog.

    Partition the same complete corpus as the positive run. A witnessed fault
    ends this negative control; all outstanding contexts are killed and joined.
    Crashes, compiler errors, malformed ledgers and timeouts are never witnesses.
    """
    cpus=len(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else (os.cpu_count() or 1)
    workers=min(4,cpus)
    ranges=[(1308*i//workers,1308*(i+1)//workers) for i in range(workers)]
    children=[];pending=set();deadline=time.monotonic()+300;witness=False
    try:
        for index,(first,limit) in enumerate(ranges):
            path=log_path.with_name(log_path.stem+'-'+str(index)+log_path.suffix)
            stream=path.open('w')
            try:child=subprocess.Popen([str(exe),'--range',str(first),str(limit)],env=env,stdout=stream,stderr=subprocess.STDOUT)
            except BaseException:stream.close();raise
            children.append((child,stream,path));pending.add(index)
        while pending and not witness:
            if time.monotonic()>=deadline:raise subprocess.TimeoutExpired(str(exe),300)
            for index in list(pending):
                child,stream,path=children[index];code=child.poll()
                if code is None:continue
                stream.flush();log=path.read_text(errors='replace');first,limit=ranges[index]
                matches=re.findall(r'fragment difference case=(\d+) scalar=(\d+) old=([0-9a-f]{8}) new=([0-9a-f]{8})',log)
                if code==1 and len(matches)==1:
                    case,scalar,old,new=matches[0]
                    assert first<=int(case)<limit and int(scalar)<320 and old!=new,(path,log)
                    witness=True;break
                if code:raise subprocess.CalledProcessError(code,child.args)
                manifests=re.findall(r'PASS ready fragment: range=(\d+):(\d+) cases=(\d+) checked=(\d+) written=(\d+) discarded=(\d+),',log)
                assert len(manifests)==1,(path,log)
                a,b,c,n,w,d=map(int,manifests[0])
                assert (a,b,c,n)==(first,limit,limit-first,(limit-first)*320),(path,log)
                pending.remove(index)
            if pending and not witness:time.sleep(.01)
        assert witness,'negative control produced no numerical difference'
    finally:
        for child,stream,path in children:
            if child.poll() is None:child.kill()
        for child,stream,path in children:child.wait();stream.close()
        with log_path.open('w') as out:
            out.write(f'Negative corpus: {workers} independent contexts, 1308 cases, shared 300s deadline; retire siblings after numerical witness\n')
            for child,stream,path in children:out.write(path.read_text(errors='replace'))

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('root',type=Path);ap.add_argument('cc',nargs='?',default='cc');ap.add_argument('--keep',type=Path);args=ap.parse_args();r=args.root.resolve()
    subprocess.run([sys.executable,str(Path(__file__).with_name('test-psh-ready-runner.py')),str(r),args.cc],check=True)
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
        for mutation in ['', 'depth', 'mode', 'target', 'volume', 'clip', 'alpha', 'combiner', 'helper', 'zero', 'final']:
            code=driver
            emitted=generator
            if mutation=='zero':
                emitted=emitted.replace('a==0u||a==6u','a==0u||a==1u||a==6u').replace('b==0u||b==6u','b==0u||b==1u||b==6u')
                assert emitted!=generator
            if mutation=='final':
                emitted=emitted.replace('combFinal0!=0x130e0300u','combFinal0!=0x130e0300u&&combFinal0!=0x130f0300u').replace('(combFinal1&~128u)!=0x1c00u','(combFinal1&0xff7fu)!=0x1c00u')
                assert emitted!=generator
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
            if not mutation:
                run_positive(exe,env,c.with_suffix('.log'))
                result=subprocess.CompletedProcess([str(exe)],0)
            else:
                run_negative(exe,env,c.with_suffix('.log'))
                result=subprocess.CompletedProcess([str(exe)],1)
            log=c.with_suffix('.log').read_text(errors='replace')
            if not mutation:
                print(log,end='',flush=True);assert result.returncode==0,(result.returncode,log)
            else:
                assert result.returncode!=0 and 'fragment difference case=' in log,(mutation,result.returncode,log)
                print('Required ready-fragment control rejected:',mutation,flush=True)
if __name__=='__main__':main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check final FP32 header assembly, false-option byte identity and faults."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile


BASELINE = '14be393b8da075543c435573c7b95100e0a2e769'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('repo',type=Path);p.add_argument('cc',nargs='?',default='cc')
    p.add_argument('--validator',default='spirv-val')
    p.add_argument('--keep',type=Path)
    a=p.parse_args();r=a.repo.resolve()
    env=dict(os.environ);env['PATH']=str(Path(shutil.which(a.cc) or a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
    original=(r/'hw/xbox/nv2a/pgraph/spv/glslc.c').read_text()
    def execute(d):
        def compile_and_run(name,source,negative=False,baseline=False):
            folder=d/name;folder.mkdir();compiler=folder/'glslc.c';compiler.write_text(source)
            exe=folder/('test.exe' if os.name=='nt' else 'test')
            cmd=[a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-missing-field-initializers',
                 '-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),
                 '-I'+str(r/'hw/xbox/nv2a/pgraph/spv'),str(Path(__file__).with_suffix('.c')),str(compiler),
                 str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),'-lm','-o',str(exe)]
            subprocess.run(cmd,check=True,env=env)
            with (folder/'run.log').open('w') as stream:
                result=subprocess.run([str(exe),str(folder)]+(['baseline'] if baseline else []),env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=60)
            text=(folder/'run.log').read_text(errors='replace')
            if negative:
                assert result.returncode!=0 and ('Assertion' in text or 'assertion' in text),(name,result.returncode,text)
                print('Required FP32 header fault rejected:',name,flush=True)
            else:
                assert result.returncode==0,(name,result.returncode,text)
                print(text,end='',flush=True)
            return folder
        current=compile_and_run('current',original)
        source=subprocess.check_output(['git','-C',str(r),'show',BASELINE+':hw/xbox/nv2a/pgraph/spv/glslc.c']).decode()
        baseline=compile_and_run('baseline-false',source,baseline=True)
        for path in baseline.glob('*.spv'):
            assert path.read_bytes()==(current/path.name).read_bytes(),path.name
        assert len(list(baseline.glob('*.spv')))==16
        print('Default-false output byte-identical to pinned pre-option compiler: 16 modules',flush=True)
        for target in range(2):
            for stage in range(4):
                for enabled in range(2):
                    assert (current/f'{target}-0-{stage}-{enabled}.spv').read_bytes()==(current/f'{target}-1-{stage}-{enabled}.spv').read_bytes()
        print('All 16 compact/prefixed final-allocation pairs are byte-identical',flush=True)
        for path in current.glob('*.spv'):
            target='vulkan1.3' if path.name.startswith('1-') else 'vulkan1.2'
            subprocess.run([a.validator,'--target-env',target,str(path)],check=True,env=env)
        print('All 32 final modules pass spirv-val',flush=True)
        controls={
            'missing-capability':('header_put(m, SpvCapabilitySignedZeroInfNanPreserve);','header_put(m, SpvCapabilityShader);'),
            'missing-mode':('header_put(m, SpvExecutionModeSignedZeroInfNanPreserve);','header_put(m, SpvExecutionModeDenormPreserve);'),
            'wrong-width':('header_put(m, SpvExecutionModeSignedZeroInfNanPreserve);\n        header_put(m, 32);','header_put(m, SpvExecutionModeSignedZeroInfNanPreserve);\n        header_put(m, 64);'),
            'wrong-entry':('header_put(m, c->main_id);\n        header_put(m, SpvExecutionModeSignedZeroInfNanPreserve);','header_put(m, 0);\n        header_put(m, SpvExecutionModeSignedZeroInfNanPreserve);'),
        }
        for name,(before,after) in controls.items():
            assert original.count(before)==1,(name,original.count(before))
            compile_and_run(name,original.replace(before,after),negative=True)
    if a.keep:
        a.keep.mkdir(parents=True,exist_ok=False);execute(a.keep.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='glslc-float-controls-') as tmp:execute(Path(tmp))


if __name__=='__main__':main()

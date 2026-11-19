#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-bound bounded Vulkan fragment admission, suffix and compiler checks.

Optional --baseline accepts a repository or frozen psh.c/psh-ready.inc directory
and makes all 5120 old ordinary/GL-ready comparisons independent. Omitting it
retains the admission/compiler proof but does not qualify old-source identity.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, shutil, struct, subprocess, sys, tempfile

spec=importlib.util.spec_from_file_location('ready_fragment_builder',Path(__file__).with_name('test-vk-ready-fragment.py'))
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)

def run(root,cc,out,baseline,seed):
 exe,env,before=builder.build(root,cc,Path(__file__).with_suffix('.c'),out,baseline=baseline,gpu=False)
 result=subprocess.run([str(exe),str(out),*([str(seed)]if seed else[])],env=env,capture_output=True,text=True,timeout=120)
 (out/'run.log').write_text(result.stdout+'\nSTDERR\n'+result.stderr)
 print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr);assert result.returncode==0
 validator=shutil.which('spirv-val',path=env.get('PATH'))
 if not validator:
  print('UNQUALIFIED: spirv-val missing; compiler outputs have not been validated',file=sys.stderr);raise SystemExit(77)
 rows=[]
 for file in sorted(out.glob('*.spv')):
  version='vulkan1.3'if '-spv1.'in file.name else 'vulkan1.2'
  checked=subprocess.run([validator,'--target-env',version,str(file)],capture_output=True,text=True,timeout=30)
  words=struct.unpack('<'+'I'*(file.stat().st_size//4),file.read_bytes());ops={};i=5
  while i<len(words):
   size=words[i]>>16;opcode=words[i]&65535;assert size;ops[opcode]=ops.get(opcode,0)+1;i+=size
  assert i==len(words)
  row=dict(file=file.name,target=version,returncode=checked.returncode,stderr=checked.stderr,implicit_samples=ops.get(87,0),selects=ops.get(169,0));rows.append(row)
  assert checked.returncode==0,checked.stdout+checked.stderr
  # Exactly four static 2D samples and four rect/four mode boolean selects;
  # scalar arithmetic mix/conditional lowering must not replace these selects.
  assert row['implicit_samples']==4 and row['selects']>=8
 assert len(rows)==8 and builder.hashes(root)==before
 (out/'spirv-validation.json').write_text(json.dumps(rows,indent=2))
 print('READY2D_CONTROLS_PASS source_recipes=1024 rejected_domains=28 raw_input_cases=2304 raw_output_cases=2592 final_cases=224 SPIRV=8; GPU numerical proof separate')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('compiler',nargs='?',default='cc');p.add_argument('--keep',type=Path);p.add_argument('--baseline',type=Path);p.add_argument('--seed',type=Path);a=p.parse_args()
 if a.keep:run(a.root.resolve(),a.compiler,a.keep.resolve(),a.baseline,a.seed)
 else:
  with tempfile.TemporaryDirectory(prefix='vk-ready-fragment-controls-')as d:run(a.root.resolve(),a.compiler,Path(d),a.baseline,a.seed)
if __name__=='__main__':main()

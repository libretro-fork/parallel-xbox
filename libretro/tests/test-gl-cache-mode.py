#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Require production cache-mode selection and its ownership fault controls."""
from pathlib import Path
import argparse
import os
import subprocess
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('root',type=Path)
parser.add_argument('cc',nargs='?',default='cc')
args=parser.parse_args()
driver=Path(__file__).with_name('test-shader-cache-handoff.py')
env=dict(os.environ,SHADER_CACHE_REQUIRE_MODE_FILTER='1')
env.pop('SHADER_CACHE_MODE_FAULT',None)
env.pop('SHADER_CACHE_SKIP_RUN',None)
env.pop('SHADER_CACHE_BINARY',None)
if os.name!='nt':env.setdefault('SHADER_CACHE_CFLAGS','-fsanitize=address,undefined')
for fault,wanted in [('',None),('eager-sso','renderer.shader_disk_thread'),
                     ('overwrite-sso','mode_reads=='),
                     ('drop-maintenance','shader_cache_writeback_complete==')]:
    run_env=dict(env)
    if fault:run_env['SHADER_CACHE_MODE_FAULT']=fault
    result=subprocess.run([sys.executable,str(driver),str(args.root),args.cc],
                          env=run_env,capture_output=True,text=True,timeout=120)
    if not fault:
        print(result.stdout,end='',flush=True)
        assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
        assert 'Shader cache modes:' in result.stdout
    else:
        assert result.returncode!=0,(fault,result.stdout,result.stderr)
        assert 'Checking cache mode sso=1 cache=1' in result.stderr,(fault,result.stderr)
        assert 'Assertion' in result.stderr and wanted in result.stderr,(fault,result.stderr)
        print('Required GL cache-mode control rejected:',fault,flush=True)

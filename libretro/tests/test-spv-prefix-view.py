#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check byte-identical module views, copy counts and allocation ownership."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve(); cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
with tempfile.TemporaryDirectory(prefix='spv-prefix-view-') as tmp:
    exe=Path(os.environ.get('SPV_PREFIX_BINARY',str(Path(tmp)/'test')))
    flags=shlex.split(os.environ.get('SPV_PREFIX_CFLAGS',''))
    includes=['-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/spv')]
    baseline=[]
    if os.environ.get('SPV_PREFIX_BASELINE_SOURCE'):
        obj=Path(tmp)/'baseline.o'
        subprocess.run(cc+['-O2','-std=gnu11',*includes,*flags,'-Dglslc_compile=baseline_glslc_compile','-c',os.environ['SPV_PREFIX_BASELINE_SOURCE'],'-o',str(obj)],check=True)
        baseline=['-DBASELINE_COMPILER',str(obj)]
    subprocess.run(cc+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-Wno-sign-compare',*includes,*flags,str(Path(__file__).with_suffix('.c')),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),*baseline,'-lm','-o',str(exe)],check=True)
    if not os.environ.get('SPV_PREFIX_SKIP_RUN'):subprocess.run([str(exe)]+([os.environ['SPV_PREFIX_OUTPUT_DIR']] if os.environ.get('SPV_PREFIX_OUTPUT_DIR') else []),check=True,timeout=60)

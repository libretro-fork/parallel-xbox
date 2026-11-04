#!/usr/bin/env python3
"""Build a private full-core code capture using one instrumented object.
The normal object and release module are restored before returning.
Requires the same compiler/dependency environment as the normal Makefile.
"""
from pathlib import Path
import os,shlex,shutil,subprocess,sys
r=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
make=['make']+sys.argv[3:]
# Refresh the normal object and its dependencies before saving/restoring it.
# Otherwise a source/header change can restore an obsolete object, or make
# can replace the private object while linking because its .d file is stale.
subprocess.run([*make,'-j8'],cwd=r,check=True)
relative='accel/tcg/translate-all.o';obj=r/relative
command=subprocess.check_output([*make,'-Bn',relative],cwd=r,text=True)
lines=[line for line in command.splitlines() if ' -c ' in line and line.rstrip().endswith('accel/tcg/translate-all.c')]
assert len(lines)==1,lines
args=shlex.split(lines[0]);args[args.index('-o')+1]=str(out/'capture.o')
args+=['-include',str(Path(__file__).with_name('tcg-code-capture.h').resolve())]
subprocess.run(args,cwd=r,check=True)
backup=out/'normal.o';shutil.copy2(obj,backup)
module=r/('parallel_xbox_libretro.dll' if any('platform=win'==a for a in make) else 'parallel_xbox_libretro.so')
try:
 shutil.copyfile(out/'capture.o',obj)
 subprocess.run([*make,'-j8'],cwd=r,check=True)
 shutil.copyfile(module,out/module.name)
finally:
 shutil.copy2(backup,obj);os.utime(obj,None)
 subprocess.run([*make,'-j8'],cwd=r,check=True)
print('Private capture module built; normal release object/module restored')

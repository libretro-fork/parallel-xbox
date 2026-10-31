# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production Windows AIO registration; native builds also transfer UDP."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('checkout',type=Path)
parser.add_argument('cc',nargs='?',default='cc')
parser.add_argument('--build-only',type=Path)
args=parser.parse_args();repo=args.checkout.resolve()
source=(repo/'util/aio-win32.c').read_text()
parts=[re.search(r'struct AioHandler \{.*?\n\};',source,re.S)[0]]
for name in ('aio_select_raw_socket','aio_set_fd_handler','aio_prepare','aio_pending','aio_dispatch_handlers'):
    m=re.search(r'^(?:static )?(?:__attribute__\(\(noinline\)\) )?(?:void|bool) '+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    body=source[m.start():end]
    parts.append(body if name in ('aio_select_raw_socket','aio_set_fd_handler') else '#ifdef _WIN32\n'+body+'\n#endif')
code=Path(__file__).with_suffix('.c').read_text().replace('/* HANDLER */',parts[0]).replace('/* PRODUCTION */','\n'.join(parts[1:]))
macros=subprocess.run([args.cc,'-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
windows='#define _WIN32 ' in macros
with tempfile.TemporaryDirectory(prefix='win32-raw-socket-') as directory:
    p=Path(directory);(p/'test.c').write_text(code)
    exe=args.build_only.resolve() if args.build_only else p/('test.exe' if windows else 'test')
    command=[args.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function',
             '-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),str(p/'test.c')]
    command+=['-static','-lws2_32'] if windows else ['-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
    subprocess.run(command+['-o',str(exe)],check=True)
    if not args.build_only:
        subprocess.run([str(exe)],check=True,timeout=30)
    else:
        print(exe)

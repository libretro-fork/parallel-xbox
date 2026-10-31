# SPDX-License-Identifier: GPL-2.0-or-later
"""Run production encoder I/O, VMState fields and guest vblank dispatch."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo=Path(sys.argv[1]).resolve()
encoder=(repo/'hw/xbox/smbus_cx25871.c').read_text()
nv2a=(repo/'hw/xbox/nv2a/nv2a.c').read_text()


def function(source,name):
    match=re.search(r'^(?:static )?(?:void|int|uint8_t) '+name+r'\([^;{]*\)\s*\{',source,re.M)
    assert match,name
    end=source.index('{',match.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]


declarations=re.search(r'typedef struct SMBusCX25871Device \{.*?\} SMBusCX25871Device;',encoder,re.S)[0]
declarations+='\nstatic SMBusCX25871Device *conexant_device;\n'
parts=[function(encoder,name) for name in ('smbus_cx25871_quick_cmd','smbus_cx25871_write_data',
       'smbus_cx25871_receive_byte','smbus_cx25871_realize','smbus_cx25871_unrealize','smbus_cx25871_notify_vblank')]
parts.append(re.search(r'static const VMStateDescription vmstate_smbus_cx25871 = \{.*?\n\};',encoder,re.S)[0])
parts.append(function(encoder,'smbus_cx25871_class_init'))
parts.append(function(nv2a,'nv2a_vga_gfx_update'))
frame=(repo/'ui/libretro.c').read_text(errors='surrogateescape')
start=frame.index('        if (!runstate_is_running()) {',frame.index('static void lr_run_frame('))
end=frame.index('        /* the guest runs to the vblank',start)
dispatch='static void dispatch_frame_vblank(bool *done,int64_t vblank_at,int64_t target) {\n'
dispatch+='bool vblank_done=*done;int64_t now;void *con=&gpu.vga;do {\n'+frame[start:end]+'\n} while(0);*done=vblank_done;\n}'
code=Path(__file__).with_suffix('.c').read_text().replace('/* DECLARATIONS */',declarations).replace('/* PRODUCTION */','\n'.join(parts)).replace('/* FRAME DISPATCH */',dispatch)
with tempfile.TemporaryDirectory(prefix='conexant-counter-') as directory:
    folder=Path(directory);(folder/'test.c').write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                    '-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
                    str(folder/'test.c'),'-o',str(folder/'test')],check=True)
    subprocess.run([str(folder/'test')],check=True,timeout=30)

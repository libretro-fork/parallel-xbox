# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production point-sprite selection across primitive/polygon modes.
Usage: python test-point-sprite-selection.py CHECKOUT [CC]
"""
from pathlib import Path
import os, re, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text()
a=s.index('static bool psh_point_sprite_enabled(')
helper=s[a:s.index('\n}',a)+2]
regs=(r/'hw/xbox/nv2a/pgraph/vsh_regs.h').read_text()
enums='\n'.join(re.search(r'enum '+name+r' \{.*?\};',regs,re.S)[0]
                for name in ('ShaderPrimitiveMode','ShaderPolygonMode'))
assert 'state->point_sprite = psh_point_sprite_enabled(raster, pg->primitive_mode);' in s
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
/* ENUMS */
#define ctz32(v) __builtin_ctz(v)
/* HELPER */
int main(void) {
    unsigned cases=0;
    for(unsigned enabled=0;enabled<2;enabled++)for(unsigned mode=0;mode<3;mode++)
    for(unsigned primitive=0;primitive<=PRIM_TYPE_POLYGON;primitive++)for(unsigned noise=0;noise<16;noise++) {
        uint32_t raster=mode|(mode<<2)|(noise<<16);
        if(enabled)raster|=NV_PGRAPH_SETUPRASTER_POINTSMOOTHENABLE;
        bool expected=enabled&&(primitive==PRIM_TYPE_POINTS||
            (primitive>=PRIM_TYPE_TRIANGLES&&mode==POLY_MODE_POINT));
        assert(psh_point_sprite_enabled(raster,primitive)==expected);cases++;
    }
    printf("PASS: %u point-sprite selections, lines/fill pass-through, polygon points and unrelated bits\n",cases);
}
'''.replace('/* ENUMS */',enums).replace('/* HELPER */',helper)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='point-sprite-selection-') as temp:
    p=Path(temp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
    command=[cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-I'+str(r)]
    subprocess.run(command+[str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)
    old='static bool psh_point_sprite_enabled(uint32_t raster,unsigned primitive) {(void)primitive;return (raster&NV_PGRAPH_SETUPRASTER_POINTSMOOTHENABLE)!=0;}'
    (p/'negative.c').write_text(code.replace(helper,old))
    subprocess.run(command+[str(p/'negative.c'),'-o',str(exe)],check=True,env=env)
    result=subprocess.run([str(exe)],capture_output=True,text=True,env=env)
    assert result.returncode!=0 and 'Assertion' in result.stderr,result.stderr
    print('PASS: unconditional sprite coordinates fail the primitive oracle')

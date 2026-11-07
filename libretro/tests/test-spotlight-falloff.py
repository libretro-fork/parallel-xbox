# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check production spotlight defaults and context-word publication.
Usage: python test-spotlight-falloff.py CHECKOUT [CC]
Numerical GPU cases are in test-point-fog.py.
"""
from pathlib import Path
import os, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(r/'hw/xbox/nv2a/pgraph/pgraph.c').read_text()
a=s.index('    const float spot_falloff_default[3]')
initial=s[a:s.index('    pg->line_width',a)]
a=s.index('    case NV097_SET_LIGHT_SPOT_FALLOFF ...')
setter=s[a:s.index('    case NV097_SET_LIGHT_SPOT_DIRECTION',a)]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef struct { uint32_t before,ltctxa[NV2A_LTCTXA_COUNT][4],after;
                 bool ltctxa_dirty[NV2A_LTCTXA_COUNT];
                 float specular_params[6],specular_params_back[6]; } PGRAPHState;
static void initialize(PGRAPHState *pg) { /* INITIAL */ }
static void write_word(PGRAPHState *pg,int slot,int part,uint32_t parameter) {
    switch(part*4) { /* SETTER */ default:assert(false); }
}
int main(void) {
    PGRAPHState pg;memset(&pg,0xa5,sizeof(pg));initialize(&pg);
    const float expected[]={0,-0.494592f,1.494592f};
    assert(!memcmp(pg.specular_params,expected,sizeof(expected)));
    assert(!memcmp(pg.specular_params_back,expected,sizeof(expected)));
    for(unsigned row=0;row<NV2A_LTCTXA_COUNT;row++) {
        bool falloff=row<NV2A_MAX_LIGHTS*2&&row%2==0;
        for(unsigned part=0;part<4;part++) {
            uint32_t value=0xa5a5a5a5;
            if(falloff&&part<3)memcpy(&value,&expected[part],4);
            assert(pg.ltctxa[row][part]==value);
        }
    }
    for(unsigned light=0;light<NV2A_MAX_LIGHTS;light++)for(unsigned part=0;part<3;part++) {
        unsigned row=NV_IGRAPH_XF_LTCTXA_L0_K+light*2;
        pg.ltctxa_dirty[row]=false;uint32_t value=0x3f000000+light*128+part;
        write_word(&pg,light,NV097_SET_LIGHT_SPOT_FALLOFF/4+part,value);
        assert(pg.ltctxa[row][part]==value&&pg.ltctxa_dirty[row]);
    }
    assert(pg.before==0xa5a5a5a5&&pg.after==0xa5a5a5a5);
    puts("PASS: eight spotlight defaults, 24 partial setter writes, context dirtiness and storage canaries");
}
'''.replace('/* INITIAL */',initial).replace('/* SETTER */',setter)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='spotlight-falloff-') as temp:
    p=Path(temp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r),str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)

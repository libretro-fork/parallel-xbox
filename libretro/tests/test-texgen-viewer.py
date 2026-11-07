# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise production viewer-mode packing without changing uniform ownership.
Usage: python test-texgen-viewer.py CHECKOUT [CC]
GPU expressions are covered by test-point-fog.py.
"""
from pathlib import Path
import os, subprocess, sys, tempfile

r=Path(sys.argv[1]).resolve(); cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
start=s.index('    if (locs[VshUniform_texgenMode] != -1) {')
end=s.index('{',start)+1; depth=1
while depth:
    depth+=(s[end]=='{')-(s[end]=='}'); end+=1
body=s[start:end]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#define ctz32(v) __builtin_ctz(v)
enum { VshUniform_texgenMode };
typedef struct { uint32_t csv0d, modes[4][4]; } PGRAPHState;
typedef struct { uint32_t before,texgenMode[4],after; } Values;
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned reg) {
    assert(reg==NV_PGRAPH_CSV0_D);return pg->csv0d;
}
static uint32_t pgraph_texgen_mode(PGRAPHState *pg,int i,int j) {
    return pg->modes[i][j];
}
static void pack(PGRAPHState *pg,const int *locs,Values *values) {
/* PRODUCTION */
}
int main(void) {
    PGRAPHState pg={0};Values v={.before=0x12345678,.after=0x98765432};
    int locs[]={0};unsigned count=0;
    for(unsigned pattern=0;pattern<4096;pattern++) {
        for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)
            pg.modes[i][j]=((pattern>>(j*3))+i)%8;
        /* Toggle only the viewer register bit, reusing the same storage. */
        for(unsigned viewer=0;viewer<3;viewer++) {
            pg.csv0d=(viewer==1?NV_PGRAPH_CSV0_D_TEXGEN_REF:0)|0x12345;
            pack(&pg,locs,&v);
            for(unsigned i=0;i<4;i++) {
                unsigned expected=viewer==1?4096:0;
                for(unsigned j=0;j<4;j++)expected|=pg.modes[i][j]<<(j*3);
                assert(v.texgenMode[i]==expected);
            }
            assert(v.before==0x12345678&&v.after==0x98765432);count++;
        }
    }
    locs[0]=-1;v.texgenMode[0]=0xdeadbeef;pack(&pg,locs,&v);
    assert(v.texgenMode[0]==0xdeadbeef);
    printf("PASS: %u texgen uniform updates, mode-only toggles, coordinate bits and storage canaries\n",count);
}
'''.replace('/* PRODUCTION */',body)
env=dict(os.environ); env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='texgen-viewer-') as temp:
    p=Path(temp); (p/'test.c').write_text(code); exe=p/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-I'+str(r),
                    str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)

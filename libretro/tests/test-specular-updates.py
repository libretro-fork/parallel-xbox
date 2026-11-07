# SPDX-License-Identifier: LGPL-2.1-or-later
"""Execute production raw specular coefficient setters and uniform publication.
Usage: python test-specular-updates.py CHECKOUT [CC]
"""
from pathlib import Path
import os, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(r/'hw/xbox/nv2a/pgraph/pgraph.c').read_text()
def body(text,marker):
    a=text.index('{',text.index(marker));b=a+1;depth=1
    while depth:depth+=(text[b]=='{')-(text[b]=='}');b+=1
    return text[a:b]
front=body(s,'DEF_METHOD_INC(NV097, SET_SPECULAR_PARAMS)')
back=body(s,'DEF_METHOD_INC(NV097, SET_SPECULAR_PARAMS_BACK)')
v=(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
publication='\n'.join('if (locs[VshUniform_'+name+'] != -1) '+body(v,'if (locs[VshUniform_'+name+'] != -1)')
                      for name in ('specularParams','specularParamsBack'))
assert 'reconstruct_specular_power(' not in s
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef struct { float specular_params[6],specular_params_back[6]; } PGRAPHState;
static void set_front(PGRAPHState *pg,uint32_t method,uint32_t parameter) /* FRONT */
static void set_back(PGRAPHState *pg,uint32_t method,uint32_t parameter) /* BACK */
enum { VshUniform_specularParams,VshUniform_specularParamsBack };
typedef struct { uint32_t before;float specularParams[1][3],specularParamsBack[1][3];uint32_t after; } Values;
static void publish(PGRAPHState *pg,int *locs,Values *values) { /* PUBLICATION */ }
int main(void) {
    PGRAPHState pg={0};Values values={.before=0x12345678,.after=0x98765432};
    int locs[]={0,0};unsigned cases=0;
    for(unsigned trial=0;trial<64;trial++)for(unsigned slot=6;slot-->0;) {
        float a=(trial+slot)/8.0f,b=-(trial+slot)/16.0f;uint32_t bits;
        memcpy(&bits,&a,4);set_front(&pg,NV097_SET_SPECULAR_PARAMS+slot*4,bits);
        memcpy(&bits,&b,4);set_back(&pg,NV097_SET_SPECULAR_PARAMS_BACK+slot*4,bits);
        assert(pg.specular_params[slot]==a&&pg.specular_params_back[slot]==b);
        publish(&pg,locs,&values);
        assert(!memcmp(values.specularParams[0],pg.specular_params,12));
        assert(!memcmp(values.specularParamsBack[0],pg.specular_params_back,12));
        assert(values.before==0x12345678&&values.after==0x98765432);cases++;
    }
    Values owned=values;pg.specular_params[0]=123;pg.specular_params_back[0]=456;
    assert(!memcmp(&values,&owned,sizeof(values)));
    locs[0]=locs[1]=-1;publish(&pg,locs,&values);
    assert(!memcmp(&values,&owned,sizeof(values)));
    locs[0]=0;publish(&pg,locs,&values);assert(values.specularParams[0][0]==123);
    assert(!memcmp(values.specularParamsBack,owned.specularParamsBack,12));
    locs[1]=0;publish(&pg,locs,&values);assert(values.specularParamsBack[0][0]==456);
    printf("PASS: %u partial/reordered coefficient publications, independent/inactive bindings, owned snapshots and canaries\n",cases);
}
'''.replace('/* FRONT */',front).replace('/* BACK */',back).replace('/* PUBLICATION */',publication)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='specular-updates-') as temp:
    p=Path(temp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
    command=[cc,'-O2','-std=c11','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-I'+str(r)]
    subprocess.run(command+[str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)

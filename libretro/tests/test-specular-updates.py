# SPDX-License-Identifier: LGPL-2.1-or-later
"""Execute production shininess reconstruction, setters and uniform binding.
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
a=s.index('struct CurveCoefficients {');b=s.index('float pgraph_get_specular_power(')
reconstruction=s[a:b].replace('static float reconstruct_specular_power(', 'static float reconstruct_impl(')
getter='float pgraph_get_specular_power(PGRAPHState *pg,bool back)'+body(s,'float pgraph_get_specular_power(')
v=(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
publication='\n'.join('if (locs[VshUniform_'+name+'] != -1) '+body(v,'if (locs[VshUniform_'+name+'] != -1)')
                      for name in ('specularPower','specularPowerBack'))
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef struct { float specular_params[6],specular_power;bool specular_power_dirty;
    float specular_params_back[6],specular_power_back;bool specular_power_back_dirty; } PGRAPHState;
/* RECONSTRUCTION */
static unsigned calls;
static float reconstruct_specular_power(const float *p) { calls++;return reconstruct_impl(p); }
/* GETTER */
static void set_front(PGRAPHState *pg,uint32_t method,uint32_t parameter) /* FRONT */
static void set_back(PGRAPHState *pg,uint32_t method,uint32_t parameter) /* BACK */
enum { VshUniform_specularPower,VshUniform_specularPowerBack };
typedef struct { float specularPower[1],specularPowerBack[1]; } Values;
static void publish(PGRAPHState *pg,int *locs,Values *values) { /* PUBLICATION */ }
static void write(PGRAPHState *pg,bool back,unsigned slot,float value) {
    uint32_t bits;memcpy(&bits,&value,4);
    if(back)set_back(pg,NV097_SET_SPECULAR_PARAMS_BACK+slot*4,bits);
    else set_front(pg,NV097_SET_SPECULAR_PARAMS+slot*4,bits);
}
int main(void) {
    PGRAPHState pg={.specular_params={0,.5f,.5f,0,0,0},.specular_power=.5f,
                   .specular_params_back={0,.5f,.5f,0,0,0},.specular_power_back=.5f};
    for(unsigned side=0;side<2;side++) {
        bool back=side!=0;unsigned before=calls;
        write(&pg,back,0,-.125f);
        assert(pgraph_get_specular_power(&pg,back)==.75f&&calls==before+1);
        assert(pgraph_get_specular_power(&pg,back)==.75f&&calls==before+1);
        float *params=back?pg.specular_params_back:pg.specular_params;
        for(unsigned slot=0;slot<6;slot++) {
            before=calls;write(&pg,back,slot,params[slot]);
            assert(pgraph_get_specular_power(&pg,back)==reconstruct_impl(params)&&calls==before+1);
        }
        before=calls;
        for(unsigned slot=6;slot-->0;)write(&pg,back,slot,params[slot]);
        assert(calls==before);assert(pgraph_get_specular_power(&pg,back)==reconstruct_impl(params));
        assert(calls==before+1);
    }
    write(&pg,false,0,-.25f);write(&pg,true,0,-.375f);
    Values values={0};int locs[]={-1,-1};unsigned before=calls;
    publish(&pg,locs,&values);assert(calls==before);
    assert(pg.specular_power_dirty&&pg.specular_power_back_dirty);
    locs[0]=0;publish(&pg,locs,&values);assert(calls==before+1);
    assert(!pg.specular_power_dirty&&pg.specular_power_back_dirty);
    assert(values.specularPower[0]==1.0f);
    locs[1]=0;publish(&pg,locs,&values);assert(calls==before+2);
    assert(values.specularPowerBack[0]==1.25f);
    puts("PASS: partial/reordered front/back updates, one reconstruction per upload, inactive bindings and cache reuse");
}
'''.replace('/* RECONSTRUCTION */',reconstruction).replace('/* GETTER */',getter).replace('/* FRONT */',front).replace('/* BACK */',back).replace('/* PUBLICATION */',publication)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='specular-updates-') as temp:
    p=Path(temp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
    command=[cc,'-O2','-std=c11','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-I'+str(r)]
    subprocess.run(command+[str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)
    negative=code.replace('pg->specular_power_dirty = true;','if(slot==5)pg->specular_power_dirty = true;').replace('pg->specular_power_back_dirty = true;','if(slot==5)pg->specular_power_back_dirty = true;')
    (p/'negative.c').write_text(negative)
    subprocess.run(command+[str(p/'negative.c'),'-o',str(exe)],check=True,env=env)
    result=subprocess.run([str(exe)],env=env,capture_output=True,text=True)
    assert result.returncode!=0 and 'Assertion' in result.stderr,result.stderr
    print('PASS: sixth-word-only invalidation fails the partial-update oracle')

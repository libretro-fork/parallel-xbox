# SPDX-License-Identifier: LGPL-2.1-or-later
"""Execute back-material uniform publication; GPU probes live in test-point-fog.py.
Usage: python test-back-lighting.py CHECKOUT [CC]
"""
from pathlib import Path
import os, subprocess, sys, tempfile

r=Path(sys.argv[1]).resolve(); cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
blocks=[]
for field in ('back_material_alpha','specularPowerBack'):
    start=s.index('if (locs[VshUniform_'+field+'] != -1) {')
    end=s.index('{',start)+1; depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    blocks.append(s[start:end])
    # These scalars must remain live; no dirty marker on their method handlers.
    sections=(r/'hw/xbox/nv2a/pgraph/glsl/uniform-cache.c').read_text()
    mapping=sections[sections.index('const int8_t pgraph_vsh_uniform_section'):sections.index('};')]
    assert '[VshUniform_'+field+']' not in mapping
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
enum { VshUniform_back_material_alpha,VshUniform_specularPowerBack };
typedef struct { float back_material_alpha,specular_power_back; } PGRAPHState;
typedef struct { uint32_t before;float back_material_alpha[1],specularPowerBack[1];uint32_t after; } Values;
static void publish(PGRAPHState *pg,int *locs,Values *values) {
/* PRODUCTION */
}
int main(void) {
    Values v={.before=0x12345678,.after=0x98765432};int locs[]={0,0};
    PGRAPHState pg={0};
    for(unsigned i=0;i<1024;i++) {
        pg.back_material_alpha=(i%17)/16.0f;pg.specular_power_back=(i%129)/2.0f;
        publish(&pg,locs,&v);
        assert(v.back_material_alpha[0]==pg.back_material_alpha);
        assert(v.specularPowerBack[0]==pg.specular_power_back);
        assert(v.before==0x12345678&&v.after==0x98765432);
    }
    for(unsigned inactive=0;inactive<2;inactive++) {
        locs[inactive]=-1;v.back_material_alpha[0]=-1;v.specularPowerBack[0]=-2;
        publish(&pg,locs,&v);
        assert(v.back_material_alpha[0]==(inactive==0?-1:pg.back_material_alpha));
        assert(v.specularPowerBack[0]==(inactive==1?-2:pg.specular_power_back));
        locs[inactive]=0;
    }
    puts("PASS: 1024 live back-material updates, independent bindings and storage canaries");
}
'''.replace('/* PRODUCTION */','\n'.join(blocks))
# Check the actual migration field descriptors, including disabled two-sided
# lighting and partial six-word shininess setters. This is a descriptor
# round trip, not a replacement for a full QEMU save/load integration test.
m=(r/'hw/xbox/nv2a/nv2a.c').read_text()
start=m.index('static bool nv2a_back_material_needed(')
end=m.index('\n};',start)+3
migration=m[start:end]
assert '&vmstate_nv2a_back_material,' in m
legacy=m[m.index('    /* Older states have no back-material subsection.'):]
legacy=legacy[:legacy.index('\n}')]
state_code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    struct { uint32_t before;float back_material_alpha,specular_power_back;
             float specular_params_back[6];uint32_t after; } pgraph;
} NV2AState;
typedef struct { size_t offset,size; } VMStateField;
typedef struct { const char *name;int version_id,minimum_version_id;
                 bool (*needed)(void *);const VMStateField *fields; } VMStateDescription;
#define VMSTATE_BUFFER_UNSAFE(f,t,v,s) {offsetof(t,f),s}
#define VMSTATE_END_OF_LIST() {0,0}
/* MIGRATION */
static int legacy_reset(NV2AState *d) {
/* LEGACY */
}
int main(void) {
    NV2AState a={0},b={0};uint8_t saved[32];
    a.pgraph.back_material_alpha=0.25f;a.pgraph.specular_power_back=32;
    for(unsigned i=0;i<6;i++)a.pgraph.specular_params_back[i]=i+0.5f;
    a.pgraph.before=b.pgraph.before=0x12345678;
    a.pgraph.after=b.pgraph.after=0x98765432;
    assert(vmstate_nv2a_back_material.needed(&a));
    size_t bytes=0;
    for(const VMStateField *f=vmstate_nv2a_back_material.fields;f->size;f++) {
        assert(bytes+f->size<=sizeof(saved));
        memcpy(saved+bytes,(char *)&a+f->offset,f->size);bytes+=f->size;
    }
    assert(bytes==32);bytes=0;
    for(const VMStateField *f=vmstate_nv2a_back_material.fields;f->size;f++) {
        memcpy((char *)&b+f->offset,saved+bytes,f->size);bytes+=f->size;
    }
    assert(!memcmp(&a,&b,sizeof(a)));
    assert(legacy_reset(&b)==0);
    assert(b.pgraph.back_material_alpha==0&&b.pgraph.specular_power_back==0);
    for(unsigned i=0;i<6;i++)assert(b.pgraph.specular_params_back[i]==0);
    assert(b.pgraph.before==a.pgraph.before&&b.pgraph.after==a.pgraph.after);
    puts("PASS: back-material migration descriptors, partial parameters and legacy defaults");
}
'''.replace('/* MIGRATION */',migration).replace('/* LEGACY */',legacy)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='back-lighting-') as temp:
    p=Path(temp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)
    (p/'state.c').write_text(state_code)
    subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror',str(p/'state.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)

# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check production lighting/point migration descriptors and uniform invalidation.
Usage: python test-lighting-point-state.py CHECKOUT [CC]
This checks descriptors, not a complete QEMU migration stream.
"""
from pathlib import Path
import os, subprocess, sys, tempfile

r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(r/'hw/xbox/nv2a/nv2a.c').read_text()
h=(r/'hw/xbox/nv2a/pgraph/pgraph.h').read_text()
fields=h[h.index('    float material_alpha;'):h.index('    /* Write markers for the uniform source')]
start=s.index('static bool nv2a_lighting_point_needed(')
migration=s[start:s.index('\n};',start)+3]
assert '&vmstate_nv2a_lighting_point,' in s
start=s.index('    /* Constants outside regs_/ltctx')
legacy=s[start:s.index('    return 0;',start)]
start=s.index('    /* The restored constants must replace')
refresh=s[start:s.index('    /* Texture registers return',start)]
vsh=(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
start=vsh.index('    for (int i = 0; i < 4; i++) {\n        state->texture_matrix_enable')
state_flags=vsh[start:vsh.index('\n    }',start)+6]
start=vsh.index('    if (locs[VshUniform_texMatEnable] != -1) {')
uniform_flags=vsh[start:vsh.index('\n    if (locs[VshUniform_inlineValue]',start)]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NV2A_MAX_LIGHTS 8
typedef struct { uint32_t before; /* FIELDS */
    bool texture_matrix_enable[4];
    bool light_dirty,point_params_dirty;uint32_t after;
} PGRAPHState;
typedef struct { PGRAPHState pgraph; } NV2AState;
typedef struct { size_t offset,size;int version; } VMStateField;
typedef struct { const char *name;int version_id,minimum_version_id;
                 bool (*needed)(void *);const VMStateField *fields; } VMStateDescription;
#define VMSTATE_BUFFER_UNSAFE(f,t,v,s) {offsetof(t,f),s,v}
#define VMSTATE_BOOL_ARRAY_V(f,t,n,v) {offsetof(t,f),sizeof(bool)*(n),v}
#define VMSTATE_END_OF_LIST() {0,0,0}
/* MIGRATION */
static void legacy_reset(NV2AState *d) { /* LEGACY */ }
static void refresh(NV2AState *d) { /* REFRESH */ }
typedef struct { bool texture_matrix_enable[4]; } FixedFunctionVshState;
static void set_flags(PGRAPHState *pg,FixedFunctionVshState *state) { /* STATE FLAGS */ }
enum { VshUniform_texMatEnable };
typedef struct { uint32_t texMatEnable[1]; } Values;
static void uniform_flags(PGRAPHState *pg,int *locs,Values *values) { /* UNIFORM FLAGS */ }
int main(void) {
    unsigned cases=0;
    for(unsigned version=1;version<=2;version++)for(unsigned trial=0;trial<256;trial++) {
        NV2AState a={0},b={0};uint8_t saved[452];size_t bytes=0;
        a.pgraph.before=b.pgraph.before=0x12345678;
        a.pgraph.after=b.pgraph.after=0x98765432;
        assert(vmstate_nv2a_lighting_point.needed(&a));
        for(unsigned i=0;i<4;i++) {
            a.pgraph.texture_matrix_enable[i]=(trial>>i)&1;
            b.pgraph.texture_matrix_enable[i]=!a.pgraph.texture_matrix_enable[i];
        }
        legacy_reset(&b);
        for(const VMStateField *f=vmstate_nv2a_lighting_point.fields;f->size;f++) {
            if(f->version>(int)version)continue;
            assert(bytes+f->size<=sizeof(saved));
            for(size_t i=0;f->offset!=offsetof(NV2AState,pgraph.texture_matrix_enable)&&i<f->size;i+=sizeof(float)) {
                float value=(float)(trial+bytes+i+1)/8.0f;
                memcpy((char *)&a+f->offset+i,&value,sizeof(value));
            }
            memcpy(saved+bytes,(char *)&a+f->offset,f->size);bytes+=f->size;
            memset((char *)&b+f->offset,0xa5,f->size);
        }
        assert(bytes==(version==1?448:452));bytes=0;
        for(const VMStateField *f=vmstate_nv2a_lighting_point.fields;f->size;f++) {
            if(f->version>(int)version)continue;
            memcpy((char *)&b+f->offset,saved+bytes,f->size);bytes+=f->size;
            assert(!memcmp((char *)&a+f->offset,(char *)&b+f->offset,f->size));
        }
        unsigned mask=version==1?0:trial&15;
        FixedFunctionVshState state={0};Values values={0};int locs[]={0};
        set_flags(&b.pgraph,&state);uniform_flags(&b.pgraph,locs,&values);
        assert(values.texMatEnable[0]==mask);
        for(unsigned i=0;i<4;i++)assert(state.texture_matrix_enable[i]==!!(mask&(1u<<i)));
        refresh(&b);assert(b.pgraph.light_dirty&&b.pgraph.point_params_dirty);
        legacy_reset(&b);
        for(const VMStateField *f=vmstate_nv2a_lighting_point.fields;f->size;f++)
            for(size_t i=0;i<f->size;i++)assert(*((uint8_t *)&b+f->offset+i)==0);
        assert(b.pgraph.before==0x12345678&&b.pgraph.after==0x98765432);cases++;
    }
    printf("PASS: %u lighting/point descriptor round trips, all texture-matrix masks, v1 defaults, static/dynamic flags and upload invalidation\n",cases);
}
'''.replace('/* FIELDS */',fields).replace('/* MIGRATION */',migration).replace('/* LEGACY */',legacy).replace('/* REFRESH */',refresh).replace('/* STATE FLAGS */',state_flags).replace('/* UNIFORM FLAGS */',uniform_flags)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='lighting-point-state-') as temp:
    p=Path(temp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)
    descriptor='VMSTATE_BOOL_ARRAY_V(pgraph.texture_matrix_enable, NV2AState, 4, 2),'
    assert descriptor in code
    (p/'negative.c').write_text(code.replace(descriptor,''))
    subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror',str(p/'negative.c'),'-o',str(exe)],check=True,env=env)
    result=subprocess.run([str(exe)],env=env,capture_output=True,text=True)
    assert result.returncode!=0 and 'Assertion' in result.stderr,result.stderr
    print('PASS: omitting texture-matrix flags fails the independent snapshot oracle')

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
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NV2A_MAX_LIGHTS 8
typedef struct { uint32_t before; /* FIELDS */
    bool light_dirty,point_params_dirty;uint32_t after;
} PGRAPHState;
typedef struct { PGRAPHState pgraph; } NV2AState;
typedef struct { size_t offset,size; } VMStateField;
typedef struct { const char *name;int version_id,minimum_version_id;
                 bool (*needed)(void *);const VMStateField *fields; } VMStateDescription;
#define VMSTATE_BUFFER_UNSAFE(f,t,v,s) {offsetof(t,f),s}
#define VMSTATE_END_OF_LIST() {0,0}
/* MIGRATION */
static void legacy_reset(NV2AState *d) { /* LEGACY */ }
static void refresh(NV2AState *d) { /* REFRESH */ }
int main(void) {
    unsigned cases=0;
    for(unsigned trial=0;trial<256;trial++) {
        NV2AState a={0},b={0};uint8_t saved[448];size_t bytes=0;
        a.pgraph.before=b.pgraph.before=0x12345678;
        a.pgraph.after=b.pgraph.after=0x98765432;
        assert(vmstate_nv2a_lighting_point.needed(&a));
        for(const VMStateField *f=vmstate_nv2a_lighting_point.fields;f->size;f++) {
            assert(bytes+f->size<=sizeof(saved));
            for(size_t i=0;i<f->size;i+=sizeof(float)) {
                float value=(float)(trial+bytes+i+1)/8.0f;
                memcpy((char *)&a+f->offset+i,&value,sizeof(value));
            }
            memcpy(saved+bytes,(char *)&a+f->offset,f->size);bytes+=f->size;
            memset((char *)&b+f->offset,0xa5,f->size);
        }
        assert(bytes==sizeof(saved));bytes=0;
        for(const VMStateField *f=vmstate_nv2a_lighting_point.fields;f->size;f++) {
            memcpy((char *)&b+f->offset,saved+bytes,f->size);bytes+=f->size;
            assert(!memcmp((char *)&a+f->offset,(char *)&b+f->offset,f->size));
        }
        refresh(&b);assert(b.pgraph.light_dirty&&b.pgraph.point_params_dirty);
        legacy_reset(&b);
        for(const VMStateField *f=vmstate_nv2a_lighting_point.fields;f->size;f++)
            for(size_t i=0;i<f->size;i++)assert(*((uint8_t *)&b+f->offset+i)==0);
        assert(b.pgraph.before==0x12345678&&b.pgraph.after==0x98765432);cases++;
    }
    printf("PASS: %u lighting/point descriptor round trips, partial constants, legacy defaults and upload invalidation\n",cases);
}
'''.replace('/* FIELDS */',fields).replace('/* MIGRATION */',migration).replace('/* LEGACY */',legacy).replace('/* REFRESH */',refresh)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='lighting-point-state-') as temp:
    p=Path(temp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)

# SPDX-License-Identifier: LGPL-2.1-or-later
"""Verify indexed input batching preserves possible GPU-only surface/cube paths."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
s=(r/'hw/xbox/nv2a/pgraph/vk/texture.c').read_text();a=s.index('static bool download_indexed_texture_inputs(');b=s.index('{',a)+1;depth=1
while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text();rt=re.search(r'typedef struct MemorySyncRequirement \{.*?\} MemorySyncRequirement;',h,re.S).group(0)
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
/* QUIET ERRORS */
#include "hw/xbox/nv2a/nv2a_regs.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
typedef uint64_t hwaddr;
typedef struct {unsigned color_format,levels;bool cubemap;} TextureShape;
typedef struct {int unused;} SurfaceBinding;
typedef struct {int unused;} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
'''+rt+r'''
static NV2AState d;static SurfaceBinding producer;
static bool exists,compatible;static unsigned lookups,checks,pair_calls,palette_calls;
static const TextureShape *expected_shape;
static SurfaceBinding *pgraph_vk_surface_get(NV2AState *device,hwaddr addr){assert(device==&d&&addr==0x123000);lookups++;return exists?&producer:NULL;}
static bool check_surface_to_texture_compatiblity(const SurfaceBinding *surface,const TextureShape *shape){assert(surface==&producer&&shape==expected_shape);checks++;return compatible;}
static void pgraph_vk_download_surface_ranges_if_dirty(PGRAPHState *pg,const MemorySyncRequirement *ranges,unsigned count){assert(pg==&d.pgraph&&count==2);assert(ranges[0].addr==0x400000&&ranges[0].size==1024&&ranges[1].addr==0x123000&&ranges[1].size==65536);pair_calls++;}
static void pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *pg,hwaddr addr,hwaddr size){assert(pg==&d.pgraph&&addr==0x400000&&size==1024);palette_calls++;}
'''
suffix=r'''
int main(void){quiet_test_errors();unsigned cases=0;for(unsigned levels=1;levels<=16;levels++)for(unsigned cube=0;cube<2;cube++)for(unsigned present=0;present<2;present++)for(unsigned match=0;match<2;match++){
 TextureShape shape={NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8,levels,cube};expected_shape=&shape;exists=present;compatible=match;lookups=checks=pair_calls=palette_calls=0;
 bool result=download_indexed_texture_inputs(&d.pgraph,&shape,0x123000,65536,0x400000,1024);
 bool expected=levels>1||(!cube&&(!present||!match));assert(result==expected&&pair_calls==expected&&palette_calls==!expected);
 assert(lookups==(levels==1&&!cube));assert(checks==(levels==1&&!cube&&present));cases++;
}printf("Indexed inputs: %u decisions passed; exact palette/index range order, mip fallback, compatible surfaces and cube preservation\n",cases);return 0;}
'''
q=(r/'libretro/tests/test-spv-geom.c').read_text();qa=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');qb=q.index('int line_tests(',qa);prefix=prefix.replace('/* QUIET ERRORS */',q[qa:qb])
with tempfile.TemporaryDirectory(prefix='vk-indexed-inputs-') as tmp:
 dpath=Path(tmp);(dpath/'test.c').write_text(prefix+s[a:b]+suffix);exe=dpath/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),str(dpath/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,timeout=30,env=env)

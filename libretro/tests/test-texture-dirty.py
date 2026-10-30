# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile both production texture dirty-page implementations against a RAM oracle."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
for api in ('gl', 'vk'):
    source = (repo / f'hw/xbox/nv2a/pgraph/{api}/texture.c').read_text()
    start = source.index('struct pgraph_texture_possibly_dirty_struct {')
    end = source.index('// Check if any of the pages', start)
    production = source[start:end]
    if api == 'vk':
        for name in ('check_texture_possibly_dirty', 'check_textures_dirty'):
            start = source.index('static bool ' + name + '(')
            end = source.index('{', start) + 1
            depth = 1
            while depth:
                depth += (source[end] == '{') - (source[end] == '}')
                end += 1
            production += source[start:end] + '\n'

    code = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "hw/xbox/nv2a/pgraph/vram-ranges.h"
typedef uint64_t hwaddr;
#define TARGET_PAGE_MASK (~(hwaddr)4095)
#define TARGET_PAGE_ALIGN(x) (((x)+4095)&TARGET_PAGE_MASK)
#define DIRTY_MEMORY_NV2A_TEX 1
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef struct LruNode { int unused; } LruNode;
#define NV2A_MAX_TEXTURES 4
typedef struct Key { hwaddr texture_vram_offset,texture_length,palette_vram_offset,palette_length; } Key;
typedef Key TextureKey;
typedef struct TextureLruNode { LruNode node; Key key; bool possibly_dirty; void *binding; unsigned int validation_frame; } TextureLruNode;
typedef TextureLruNode TextureBinding;
typedef struct Lru { TextureBinding nodes[4]; unsigned visits; } Lru;
typedef struct Renderer { Lru texture_cache; TextureBinding *texture_bindings[4]; } PGRAPHGLState;
typedef PGRAPHGLState PGRAPHVkState;
typedef struct PGRAPHState { PGRAPHGLState *gl_renderer_state,*vk_renderer_state; bool texture_dirty[4],enabled[4]; unsigned frame_time; } PGRAPHState;
static bool pgraph_is_texture_enabled(PGRAPHState *pg,unsigned i) { return pg->enabled[i]; }
typedef struct RAM { hwaddr size; bool dirty[4]; unsigned queries; } RAM;
typedef struct NV2AState { PGRAPHState pgraph; RAM *vram; } NV2AState;
static hwaddr memory_region_size(RAM *r) { return r->size; }
static bool memory_region_test_and_clear_dirty(RAM *r,hwaddr a,hwaddr n,int kind) {
    assert(kind==1 && a%4096==0 && n && n%4096==0 && a+n<=r->size);
    r->queries++; bool result=false;
    for (unsigned i=a/4096;i<(a+n)/4096;i++) { result |= r->dirty[i]; r->dirty[i]=false; }
    return result;
}
static void lru_visit_active(Lru *l,void (*fn)(Lru *,LruNode *,void *),void *p) {
    l->visits++; for(unsigned i=0;i<4;i++) fn(l,&l->nodes[i].node,p);
}
/* PRODUCTION */
int main(void) {
    RAM ram={.size=16384}; PGRAPHGLState r={0};
    NV2AState d={.pgraph={.gl_renderer_state=&r,.vk_renderer_state=&r},.vram=&ram};
    assert(pgraph_vram_range_valid(16383,1,16384));
    assert(pgraph_vram_range_valid(16384,0,16384));
    assert(!pgraph_vram_range_valid(16384,1,16384));
    assert(!pgraph_vram_range_valid(UINT64_MAX,2,UINT64_MAX));
    for(unsigned i=0;i<4;i++) r.texture_cache.nodes[i].binding=&r;
    /* Texture, palette, preceding page, and empty cache entry. */
    r.texture_cache.nodes[0].key=(Key){16383,1,0,0};
    r.texture_cache.nodes[1].key=(Key){0,1,12288,1024};
    r.texture_cache.nodes[2].key=(Key){12287,1,0,0};
    r.texture_cache.nodes[3].key=(Key){13000,0,13000,0};
    assert(!check_texture_dirty(&d,16384,0));
    MARK(&d,13000,0);
    assert(!ram.queries && !r.texture_cache.visits);
    ram.dirty[3]=true;
    assert(check_texture_dirty(&d,16383,1));
    assert(!check_texture_dirty(&d,12288,4096));
    MARK(&d,16383,1);
    assert(r.texture_cache.nodes[0].possibly_dirty);
    assert(r.texture_cache.nodes[1].possibly_dirty);
    assert(!r.texture_cache.nodes[2].possibly_dirty);
    assert(!r.texture_cache.nodes[3].possibly_dirty);
    for(unsigned i=0;i<4;i++) r.texture_cache.nodes[i].possibly_dirty=false;
    MARK(&d,12287,2);
    assert(r.texture_cache.nodes[0].possibly_dirty);
    assert(r.texture_cache.nodes[1].possibly_dirty);
    assert(r.texture_cache.nodes[2].possibly_dirty);
    assert(!r.texture_cache.nodes[3].possibly_dirty);
    puts("API texture/palette final-page, shared-page, cross-page and empty-span cases passed");
}
'''.replace('/* PRODUCTION */', production).replace('MARK', f'pgraph_{api}_mark_textures_possibly_dirty').replace('API', api)
    if api == 'vk':
        code = code.replace('    puts("vk texture/palette', r'''    for (unsigned i=0;i<4;i++) {
        r.texture_cache.nodes[i].validation_frame=7;
        r.texture_cache.nodes[i].possibly_dirty=true;
    }
    pgraph_vk_mark_textures_possibly_dirty(&d,16383,1);
    assert(r.texture_cache.nodes[0].validation_frame == (unsigned)-1);
    assert(r.texture_cache.nodes[1].validation_frame == (unsigned)-1);
    r.texture_cache.nodes[0].validation_frame=7;
    r.texture_cache.nodes[1].validation_frame=7;
    pgraph_vk_invalidate_textures_from_gpu(&d,16383,1);
    assert(r.texture_cache.nodes[0].validation_frame == (unsigned)-1);
    assert(r.texture_cache.nodes[1].validation_frame == (unsigned)-1);
    assert(r.texture_cache.nodes[2].validation_frame == 7);
    assert(r.texture_cache.nodes[3].validation_frame == 7);
    puts("vk texture/palette''')
    if api == 'gl':
        code = code.replace('static bool pgraph_is_texture_enabled(PGRAPHState *pg,unsigned i) { return pg->enabled[i]; }', '')
    if api == 'vk':
        code = code.replace('    puts("vk texture/palette', r'''    TextureBinding dummy={0};
    for (unsigned i=0;i<4;i++) {
        r.texture_bindings[i]=&dummy;
        ram.dirty[i]=false;
    }
    r.texture_bindings[0]=&r.texture_cache.nodes[0];
    r.texture_bindings[0]->possibly_dirty=false;
    d.pgraph.frame_time=7;
    r.texture_bindings[0]->validation_frame=7;
    d.pgraph.enabled[0]=true;
    ram.queries=0;
    assert(!check_textures_dirty(&d));
    assert(ram.queries==1);
    ram.dirty[3]=true;
    assert(check_textures_dirty(&d));
    assert(!ram.dirty[3]);
    assert(r.texture_bindings[0]->possibly_dirty);
    assert(r.texture_bindings[0]->validation_frame==(unsigned)-1);
    ram.queries=0;
    assert(check_textures_dirty(&d));
    assert(!ram.queries);
    r.texture_bindings[0]=&dummy;
    d.pgraph.enabled[0]=false;
    ram.dirty[3]=true;
    assert(!check_textures_dirty(&d));
    assert(!ram.queries && ram.dirty[3]);
    for (unsigned i=0;i<4;i++) {
        r.texture_cache.nodes[i].possibly_dirty=false;
        r.texture_cache.nodes[i].validation_frame=7;
    }
    r.texture_bindings[0]=&r.texture_cache.nodes[0];
    r.texture_bindings[1]=&r.texture_cache.nodes[1];
    r.texture_cache.nodes[1].key.palette_vram_offset=16380;
    r.texture_cache.nodes[1].key.palette_length=4;
    r.texture_bindings[2]=&r.texture_cache.nodes[2];
    r.texture_cache.nodes[2].key.texture_vram_offset=14000;
    r.texture_bindings[3]=&r.texture_cache.nodes[3];
    pgraph_vk_invalidate_bound_textures(&d.pgraph,16383,1);
    assert(!ram.queries);
    assert(r.texture_cache.nodes[0].possibly_dirty);
    assert(r.texture_cache.nodes[1].possibly_dirty);
    assert(r.texture_cache.nodes[0].validation_frame==(unsigned)-1);
    assert(r.texture_cache.nodes[1].validation_frame==(unsigned)-1);
    assert(!r.texture_cache.nodes[2].possibly_dirty);
    assert(!r.texture_cache.nodes[3].possibly_dirty);
    r.texture_cache.nodes[0].possibly_dirty=false;
    r.texture_cache.nodes[1].possibly_dirty=false;
    r.texture_bindings[2]=NULL;
    pgraph_vk_invalidate_bound_textures(&d.pgraph,16383,0);
    assert(!r.texture_cache.nodes[0].possibly_dirty);
    assert(!r.texture_cache.nodes[1].possibly_dirty);
    puts("vk texture/palette''')
    with tempfile.TemporaryDirectory(prefix='texture-dirty-', dir=repo.parent) as directory:
        folder = Path(directory)
        (folder/'test.c').write_text(code)
        exe=folder/('test.exe' if os.name=='nt' else 'test')
        subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Wno-unused-parameter',
                        '-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
                        '-I',str(repo),str(folder/'test.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

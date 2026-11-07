# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production GL reuse decision with pending CPU-written sources."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
source = (repo / 'hw/xbox/nv2a/pgraph/gl/texture.c').read_text()
start = source.index('        if (!pg->texture_dirty[i] && tbind &&')
end = source.index('            if (reusable) {', start)
decision = source[start:end] + 'return reusable; } return false;'
code = r'''
#include <stdbool.h>
#include <stdio.h>
typedef struct Surface { int draw_time; bool upload_pending; } SurfaceBinding;
typedef struct Texture { int draw_time; bool g8b8_lod_clamp, byte_reinterpret; } TextureBinding;
typedef struct State { bool texture_dirty[1]; } State;
static bool dirty;
static bool check_texture_possibly_dirty(void *d, int a, int b, int c, int e) {
    (void)d; (void)a; (void)b; (void)c; (void)e; return dirty;
}
static bool reuse(State *pg, TextureBinding *tbind, SurfaceBinding *surface,
                  SurfaceBinding **cube_faces, bool surf_to_tex_cube,
                  int cube_draw_time, bool surf_to_tex_bytes) {
    int i=0, texture_vram_offset=0, length=0, palette_vram_offset=0, palette_length=0;
    bool g8b8_lod_clamp=false, is_indexed=false, possibly_dirty=false;
    bool possibly_dirty_checked=false; void *d=0;
    (void)possibly_dirty_checked;
    /* DECISION */
}
int main(void) {
    State pg={0}; TextureBinding t={.draw_time=10};
    SurfaceBinding faces[6]; SurfaceBinding *ptrs[6];
    unsigned checks=0;
    for (unsigned cube=0; cube<2; cube++)
    for (unsigned mask=0; mask<64; mask++)
    for (unsigned changed=0; changed<2; changed++)
    for (unsigned state_dirty=0; state_dirty<2; state_dirty++) {
        for (unsigned f=0; f<6; f++) {
            faces[f]=(SurfaceBinding){10, (mask & (1u<<f))!=0}; ptrs[f]=&faces[f];
        }
        pg.texture_dirty[0]=state_dirty;
        faces[0].draw_time=10+changed;
        bool actual=reuse(&pg,&t,&faces[0],ptrs,cube,10+changed,false);
        bool expected=!state_dirty && !changed && !(cube ? mask : (mask & 1));
        if (actual!=expected) return 1;
        checks++;
    }
    pg.texture_dirty[0]=false;
    for (unsigned pending=0; pending<2; pending++) {
        faces[0]=(SurfaceBinding){10,pending}; t.byte_reinterpret=true;
        if (reuse(&pg,&t,&faces[0],ptrs,false,10,true)==pending) return 2;
        checks++;
    }
    t.byte_reinterpret=false;
    for (unsigned ram_dirty=0; ram_dirty<2; ram_dirty++) {
        dirty=ram_dirty;
        if (reuse(&pg,&t,0,ptrs,false,0,false)==ram_dirty) return 3;
        checks++;
    }
    printf("GL pending-upload reuse: %u cases passed\n",checks);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='gl-pending-upload-') as directory:
    directory = Path(directory)
    for negative in (False, True):
        body = decision
        if negative:
            body = body.replace('!(surface && surface->upload_pending)',
                                '!(surf_to_tex_bytes && surface->upload_pending)')
            body = body.replace('reusable &= !cube_faces[fc]->upload_pending;',
                                '(void)cube_faces[fc];')
            assert body != decision
        path = directory / 'test.c'
        path.write_text(code.replace('/* DECISION */', body))
        exe = directory / ('test.exe' if os.name == 'nt' else 'test')
        subprocess.run([cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                        str(path), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)])
        if negative:
            assert result.returncode != 0, 'old reuse decision escaped regression'
            print('Pre-fix reuse decision rejected by regression')
        else:
            result.check_returncode()

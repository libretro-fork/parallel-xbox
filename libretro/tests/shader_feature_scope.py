# SPDX-License-Identifier: GPL-2.0-or-later
"""Select introduced feature contracts without forgiving broken advertised coverage."""
from pathlib import Path
import sys

def ready_ff_scope(root):
    root=Path(root)
    runtime=[root/'hw/xbox/nv2a/pgraph/gl'/name for name in ['ready-ff.inc','ready-ff-state.h']]
    advertised=any('ready_ff' in (root/path).read_text() for path in [
        'hw/xbox/nv2a/pgraph/gl/renderer.h','hw/xbox/nv2a/pgraph/gl/shaders.c'])
    present=[p.is_file() for p in runtime]
    if not any(present) and not advertised:
        return 'historical'
    if not all(present):
        raise ValueError('Advertised ready-FF implementation is incomplete')
    if 'GL_READY_FF_STAGE_COUNT' not in runtime[1].read_text():
        raise ValueError('Ready-FF stage contract is missing')
    fixtures=['test-gl-ready-ff-owners.py','test-gl-ready-ff-owners.c',
              'test-gl-ready-ff-source.py','test-gl-ready-ff-source.c',
              'test-gl-ready-ff-unlit.py']
    for name in fixtures:
        if not (root/'libretro/tests'/name).is_file():
            raise ValueError('Selected ready-FF revision has no fixture: '+name)
    return 'present'

if __name__=='__main__':
    print(ready_ff_scope(Path(sys.argv[1]).resolve()))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare the actual derived fixtures and retain their independent domains."""
from pathlib import Path
import importlib.util
import sys
import tempfile


def load(name):
    path=Path(__file__).with_name(name+'.py')
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


root=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='vsh-derived-domains-') as tmp:
    directory=Path(tmp)
    for name,backend in [('test-vsh-source-owners',None),
                         ('test-vsh-interpreter-gpu',None),
                         ('test-vsh-interpreter-dynamic','vk'),
                         ('test-vsh-interpreter-dynamic','gl')]:
        folder=directory/(name+(backend or ''));folder.mkdir()
        fixture=load(name)
        if backend:fixture.prepare(root,folder,backend)
        else:fixture.prepare(root,folder)
        script=(folder/'driver.py').read_text()
        code=(folder/'driver.c').read_text()
        if 'DIRECT_VERTEX_FIXTURE =' in script:
            assert 'DIRECT_VERTEX_FIXTURE = False' in script,name
            assert 'DIRECT_VERTEX_FIXTURE = True' not in script,name
        assert '  fixture_note_case(test);' not in code,name
        if backend:
            assert '(42512, 42896)' in script,(name,backend)
            assert 'limit<=42896' in code,(name,backend)
        print('Independent fixture domain:',name,backend or 'CPU/Vulkan')
print('PASS actual CPU, Vulkan and GL derived preparation; extended corpus retained')

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Required CPU-only ledger, watchdog and process-retirement controls.

Synthetic child manifests test the aggregator; the separate GPU corpus must
still execute every case and independent oracle on real GL contexts.
"""
from pathlib import Path
import contextlib
import importlib.util
import io
import os
import shutil
import subprocess
import sys
import tempfile
import types

r = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
path = Path(__file__).with_name('test-psh-ready-gl.py')
spec = importlib.util.spec_from_file_location('ready_fragment_runner', path)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
env = dict(os.environ)
env['PATH'] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + env.get('PATH', '')
original_popen = subprocess.Popen
affinity_name = 'sched_getaffinity' if hasattr(os, 'sched_getaffinity') else 'cpu_count'
original_affinity = getattr(os, affinity_name)
original_time = runner.time
with tempfile.TemporaryDirectory(prefix='ready-fragment-ledger-') as tmp:
    d = Path(tmp)
    assert d.resolve().parent == Path(tempfile.gettempdir()).resolve()
    assert d.name.startswith('ready-fragment-ledger-')
    exe = d / ('worker.exe' if os.name == 'nt' else 'worker')
    subprocess.run([cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    str(Path(__file__).with_suffix('.c')), '-o', str(exe)], check=True, env=env)
    scenarios = [(1, ''), (2, ''), (4, ''), (8, '')]
    scenarios += [(4, fault) for fault in ['checks', 'range', 'cases', 'oracle',
                                         'duplicate', 'pixels', 'exit', 'timeout', 'spawn']]
    for cpus, fault in scenarios:
        started = []
        child_env = dict(env, READY_RUNNER_FAULT=fault)
        if affinity_name == 'sched_getaffinity':
            os.sched_getaffinity = lambda pid: set(range(cpus))
        else:
            os.cpu_count = lambda: cpus
        if fault == 'timeout':
            ticks = iter([0] + [301] * 10)
            runner.time = types.SimpleNamespace(monotonic=lambda: next(ticks))

        def launch(*args, **kwargs):
            if fault == 'spawn' and started:
                raise OSError('deliberate worker launch failure')
            child = original_popen(*args, **kwargs)
            started.append(child)
            return child

        subprocess.Popen = launch
        try:
            error = None
            with contextlib.redirect_stdout(io.StringIO()):
                try:
                    runner.run_positive(exe, child_env, d / f'{cpus}-{fault or "valid"}.log')
                except (AssertionError, subprocess.CalledProcessError,
                        subprocess.TimeoutExpired, OSError) as failure:
                    error = type(failure).__name__
            assert bool(error) == bool(fault), (cpus, fault, error)
            assert len(started) == (1 if fault == 'spawn' else min(4, cpus)), (cpus, fault, len(started))
            assert all(child.poll() is not None for child in started)
        finally:
            subprocess.Popen = original_popen
            setattr(os, affinity_name, original_affinity)
            runner.time = original_time
print('PASS ready-fragment runner: 1/2/4/capped contexts, nine required ledger/failure controls; CPU-only metadata checks', flush=True)

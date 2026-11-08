#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""An improvement elsewhere cannot pay for one shader/target growing."""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('bench', Path(__file__).with_name('benchmark-shader-compile.py'))
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)
before = {(0, 0): 100, (0, 1): 100, (1, 0): 200}
assert bench.module_growth(before, dict(before)) == []
assert bench.module_growth(before, {k: v - 1 for k, v in before.items()}) == []
after = {(0, 0): 101, (0, 1): 99, (1, 0): 1}
assert sum(after.values()) < sum(before.values())
assert bench.module_growth(before, after) == [dict(module=0, target=0, baseline=100, candidate=101)]
for invalid in ({(0, 0): 100}, {**before, (2, 0): 1}, {**before, (0, 1): 0}):
    try:
        bench.module_growth(before, invalid)
    except ValueError:
        pass
    else:
        raise AssertionError('invalid module comparison accepted')
print('Compiler gate: exact input/target coverage, positive counts and per-module ratchet PASS')

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""An improvement elsewhere cannot pay for one shader/target growing."""
import importlib.util
import itertools
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

cases = 0
severity = {'pass': 0, 'inconclusive': 1, 'regression': 2}
names = list(severity)
for statuses in itertools.product(names, repeat=4):
    for flags in itertools.product((False, True), repeat=4):
        word, instruction, shader, noisy = flags
        expected = names[max([severity[s] for s in statuses] +
                             [2 if word or instruction or shader else 0, int(noisy)])]
        actual = bench.comparison_status(statuses, word_growth=word,
                    instruction_growth=instruction, shader_regressions=shader, noisy_shaders=noisy)
        assert actual == expected, (statuses, flags, actual, expected)
        cases += 1
for invalid in ([], ['missing'], ['pass', None], ['regression', 'unknown']):
    try:
        bench.comparison_status(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError('invalid qualification accepted')
# Reproduce the old order: it rejected the gate but mislabeled a proven
# regression whenever another comparison was inconclusive.
def old_status(statuses, shader=False, noisy=False):
    return ('inconclusive' if 'inconclusive' in statuses or noisy else
            'regression' if 'regression' in statuses or shader else 'pass')
for statuses, shader, noisy in [(['regression', 'inconclusive'], False, False),
                               (['pass', 'inconclusive'], True, False),
                               (['regression', 'pass'], False, True)]:
    assert old_status(statuses, shader, noisy) == 'inconclusive'
    assert bench.comparison_status(statuses, shader_regressions=shader, noisy_shaders=noisy) == 'regression'
print(f'Compiler gate: {cases} aggregate/stage/shader/code-growth/noise combinations PASS; old downgrade controls rejected')

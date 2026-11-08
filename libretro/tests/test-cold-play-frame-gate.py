"""Required rejection controls for the callback-evidence gate. No retail assets.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
tool = root / 'libretro/tests/gate-cold-play-frames.py'
spec = importlib.util.spec_from_file_location('gate', tool)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
head, reference = 'a' * 40, 'b' * 40
matrix = {'include': [dict(commit=head, reference=reference, kind='parent')]}


def fixture(path, commit=head, ref=reference, kind='parent'):
    runs = []
    for renderer in gate.RENDERERS:
        for cache in gate.CACHES:
            for block in range(2):
                for slot in range(4):
                    revision = ref if slot in (0, 3) else commit
                    name = f'{commit[:8]}-{kind}-{renderer}-{cache}-{block}-{slot}.csv'
                    runs.append(dict(schema=1, commit=commit, reference=ref, kind=kind,
                                     revision=revision, renderer=renderer, cache=cache, block=block,
                                     slot=slot, file=name, hardware='synthetic-only',
                                     options_sha256='c' * 64, binary_sha256=revision[0] * 64,
                                     clean_core=True, fresh_process=True, overlapping_work=False,
                                     image_readbacks=0, image_allocations=0,
                                     driver_cache=('isolated-empty' if cache == 'cache-empty' else
                                                   'warmed-controlled'), case='doa3-first-fights'))
                    # Synthetic fresh boot/menu, intro, 600 consecutive live
                    # callbacks. A larger boot sample prevents a mean regression
                    # test from merely tripping the whole-boot maximum check.
                    rows = [[0, 800, 1, 10000, 0, 255, 255, 0, 0, 0, 0],
                            [1, 801, 1, 5000, 20000, 0, 0, 0, 0, 0, 0],
                            [2, 801, 1, 5000, 40000, 1, 10, 0, 0, 240, 0]]
                    rows += [[i+2, 801, 1, 5000, (i+2)*20000, 1, 10, 0, 1, 240, i]
                             for i in range(1, 601)]
                    for i, row in enumerate(rows):
                        row[1] = (i + 1) * 4004 // 5 - i * 4004 // 5
                    write_rows(path / name, rows)
    return runs


def write_rows(path, rows):
    # Rebuild the synthetic clock after an injected stall.
    start = 0
    with path.open('w') as output:
        for row in rows:
            row[4] = start
            start += row[3] + 10
            output.write(','.join(map(str, row)) + '\n')


def change_frames(path, run, change):
    with (path / run['file']).open() as source:
        rows = [list(map(int, row.split(','))) for row in source]
    change(rows)
    write_rows(path / run['file'], rows)


def first_candidate(runs):
    return next(r for r in runs if r['slot'] == 1)


def expect(label, expected, mutate=lambda p, r: None, matrix_override=None, accepted=reference):
    with tempfile.TemporaryDirectory(prefix='callback-gate-') as temporary:
        path = Path(temporary)
        runs = fixture(path)
        mutate(path, runs)
        with (path / 'callback-runs.jsonl').open('w') as output:
            for run in runs:
                output.write(json.dumps(run) + '\n')
        try:
            result = gate.audit(path, matrix_override or matrix, accepted)
        except (ValueError, OSError, TypeError, KeyError) as error:
            result = {'status': 'unqualified', 'error': str(error)}
        assert result['status'] == expected, (label, expected, result)
        if label == 'stable mean regression':
            assert any(c['status'] == 'regression' for c in result['comparisons'])
            assert not result['failures'], 'must exercise the mean gate, not a max/hitch shortcut'
        if label == 'bounded hitch summary retains total and worst':
            failure = next(f for f in result['failures'] if 'miss_count' in f)
            assert failure['miss_count'] == 600 and len(failure['misses']) == 64
            assert failure['misses'][0]['duration_us'] == 116504
        print(label + ': ' + expected)


expect('synthetic valid evidence (not hardware qualification)', 'pass')


def add_accepted(path, runs):
    runs.extend(fixture(path, ref='d' * 40, kind='accepted'))


expect('distinct accepted comparison', 'pass', add_accepted,
       matrix_override={'include': matrix['include'] +
                        [dict(commit=head, reference='d' * 40, kind='accepted')]}, accepted='d' * 40)


def add_commit(path, runs):
    runs.extend(fixture(path, commit='e' * 40, ref=head))
    runs.extend(fixture(path, commit='e' * 40, kind='accepted'))


expect('every commit in a multi-commit push', 'pass', add_commit,
       matrix_override={'include': matrix['include'] +
                        [dict(commit='e' * 40, reference=head, kind='parent'),
                         dict(commit='e' * 40, reference=reference, kind='accepted')]})
for label, duration, index in (('retained 116.504 ms live hitch', 116504, 400),
                               ('retained 170.166 ms live hitch', 170166, 400),
                               ('retained 17.421 ms intro hitch', 17421, 2)):
    expect(label, 'regression', lambda p, r, d=duration, i=index:
           change_frames(p, first_candidate(r), lambda rows: rows[i].__setitem__(3, d)))
expect('reference hitch cannot become accepted', 'regression', lambda p, r:
       change_frames(p, r[0], lambda rows: rows[400].__setitem__(3, 170166)))
expect('reference intro hitch cannot hide in averages', 'regression', lambda p, r:
       change_frames(p, r[0], lambda rows: rows[2].__setitem__(3, 17421)))
expect('boot spike is retained', 'regression', lambda p, r:
       change_frames(p, first_candidate(r), lambda rows: rows[0].__setitem__(3, 30000)))
expect('bounded hitch summary retains total and worst', 'regression', lambda p, r:
       change_frames(p, first_candidate(r), lambda rows:
                     ([row.__setitem__(3, 20000) for row in rows[3:]], rows[400].__setitem__(3, 116504))))


def mean_regression(path, runs):
    for run in runs:
        if run['slot'] in (1, 2):
            change_frames(path, run, lambda rows: [row.__setitem__(3, 5500) for row in rows[1:]])


expect('stable mean regression', 'regression', mean_regression)
expect('noisy reference is not a pass', 'unqualified', lambda p, r:
       change_frames(p, r[0], lambda rows: [row.__setitem__(3, 6000) for row in rows[1:]]))
for name, value in (('driver_cache', 'uncontrolled'), ('clean_core', False),
                    ('fresh_process', False), ('overlapping_work', True),
                    ('image_readbacks', 1), ('image_allocations', 1),
                    ('image_readbacks', False), ('slot', True),
                    ('revision', reference), ('hardware', ''),
                    ('options_sha256', 'f' * 64), ('binary_sha256', 'f' * 64),
                    ('case', 'checkpoint-only'), ('file', '../private.csv'),
                    ('schema', 2), ('passed', True)):
    expect('reject ' + name + '=' + str(value), 'unqualified',
           lambda p, r, k=name, v=value: first_candidate(r).__setitem__(k, v))
expect('missing run', 'unqualified', lambda p, r: r.pop())
expect('duplicate run', 'unqualified', lambda p, r: r.append(copy.deepcopy(r[0])))
expect('missing accepted reference', 'unqualified', accepted='')
expect('uncovered accepted reference', 'unqualified', accepted='d' * 40)
expect('missing frame file', 'unqualified', lambda p, r: (p / r[0]['file']).unlink())
expect('incomplete progression', 'unqualified', lambda p, r:
       change_frames(p, r[0], lambda rows: rows.pop()))
expect('counter jump', 'unqualified', lambda p, r:
       change_frames(p, r[0], lambda rows: rows[400].__setitem__(10, 600)))
expect('checkpoint bypasses boot', 'unqualified', lambda p, r:
       change_frames(p, r[0], lambda rows: rows[1].__setitem__(5, 1)))
expect('audio loss', 'unqualified', lambda p, r:
       change_frames(p, r[0], lambda rows: rows[400].__setitem__(1, 0)))
expect('audio drift within 800/801 range', 'unqualified', lambda p, r:
       change_frames(p, r[0], lambda rows: rows[401].__setitem__(1, 800)))
expect('skipped video', 'unqualified', lambda p, r:
       change_frames(p, r[0], lambda rows: rows[400].__setitem__(2, 0)))
expect('callback hole', 'unqualified', lambda p, r:
       change_frames(p, r[0], lambda rows: rows[400].__setitem__(0, 401)))
expect('oversized CSV record', 'unqualified', lambda p, r:
       (p / r[0]['file']).write_text('1' * 4097))

# The CI CLI must fail closed and leave a summary for absent evidence.
with tempfile.TemporaryDirectory(prefix='callback-gate-cli-') as temporary:
    path = Path(temporary)
    command = [sys.executable, str(tool), '--report', str(path), '--revisions-json',
               json.dumps(matrix), '--accepted', reference]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 2 and json.loads((path / 'callback-gate.json').read_text())['status'] == 'unqualified'
    (path / 'callback-runs.jsonl').write_text('{"schema":1,"schema":1}\n')
    assert subprocess.run(command, capture_output=True).returncode == 2
    (path / 'callback-runs.jsonl').write_text(' ' * 16385)
    assert subprocess.run(command, capture_output=True).returncode == 2
    runs = fixture(path)
    with (path / 'callback-runs.jsonl').open('w') as output:
        for run in runs:
            output.write(json.dumps(run) + '\n')
    assert subprocess.run(command, capture_output=True).returncode == 0
    change_frames(path, first_candidate(runs), lambda rows: rows[400].__setitem__(3, 116504))
    assert subprocess.run(command, capture_output=True).returncode == 1
    assert json.loads((path / 'callback-gate.json').read_text())['failures']
print('CLI missing/duplicate-key/oversized evidence rejected; no assets or hardware pass fabricated')

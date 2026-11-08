"""Paired retail-checkpoint throughput gate; assets stay outside the repository.

Exit 0: pass; 1: reproducible regression; 2: invalid/noisy experiment.
Run without other emulator processes. See performance-regressions.md.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess


def assess(baseline, candidate, limit=0.08, noise=0.05):
    """Each block contains two baseline and two candidate process means."""
    if len(baseline) != len(candidate) or len(baseline) < 4 or len(baseline) % 2:
        raise ValueError('at least two complete ABBA blocks are required')
    if any(not math.isfinite(x) or x <= 0 for x in baseline + candidate):
        raise ValueError('timings must be finite and positive')
    ratios = [statistics.mean(candidate[i:i+2]) / statistics.mean(baseline[i:i+2])
              for i in range(0, len(baseline), 2)]
    slowdown = statistics.median(candidate) / statistics.median(baseline) - 1
    unstable = any((max(x)-min(x)) / statistics.median(x) > noise
                   for x in (baseline, candidate))
    if unstable or (slowdown > limit and not all(x > 1+limit for x in ratios)):
        status = 'inconclusive'
    else:
        status = 'regression' if slowdown > limit else 'pass'
    return dict(status=status, slowdown=slowdown, block_ratios=ratios,
                baseline_fps=1000000/statistics.median(baseline),
                candidate_fps=1000000/statistics.median(candidate))


def timings(path, frames, segment, warmup):
    with path.open() as source:
        rows = list(csv.reader(source))
    if len(rows) != frames:
        raise ValueError('incomplete CSV')
    samples = []
    for expected, row in enumerate(rows):
        if len(row) < 4 or int(row[0]) != expected:
            raise ValueError('invalid CSV sequence')
        duration = float(row[3])
        if not math.isfinite(duration) or duration <= 0:
            raise ValueError('invalid callback duration')
        # Checkpoint is loaded before run 1, then 1+segment, etc.
        if expected >= 1 and (expected-1) % segment >= warmup:
            samples.append(duration)
    if not samples:
        raise ValueError('no measured samples')
    return statistics.mean(samples)


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('harness', 'baseline', 'candidate', 'system', 'content', 'state', 'options', 'output'):
        p.add_argument('--'+key, type=Path, required=True)
    p.add_argument('--renderer', choices=('vulkan', 'gl'), default='vulkan')
    p.add_argument('--frames', type=int, default=600)
    p.add_argument('--segment', type=int, default=200)
    p.add_argument('--warmup', type=int, default=30)
    p.add_argument('--blocks', type=int, default=2)
    p.add_argument('--max-slowdown', type=float, default=0.08)
    p.add_argument('--max-noise', type=float, default=0.05)
    a = p.parse_args()
    if (a.blocks < 2 or a.segment <= a.warmup or a.warmup < 1 or
            a.frames < 2*a.segment or not 0 < a.max_slowdown < 1 or
            not 0 < a.max_noise < 1):
        p.error('require >=2 blocks, >=2 segments, warmup within segment and positive limits <1')
    paths = {k: getattr(a, k).resolve() for k in
             ('harness', 'baseline', 'candidate', 'system', 'content', 'state', 'options', 'output')}
    # Keep a separate, new result directory: do not overwrite a prior experiment.
    paths['output'].mkdir(parents=True, exist_ok=False)
    opts = []
    for line in paths['options'].read_text().splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        match = re.fullmatch(r'\s*(\w+)\s*=\s*"([^"]*)"\s*', line)
        if not match:
            raise ValueError('invalid core-options line: '+line)
        opts += ['--opt', match[1]+'='+match[2]]
    results = {'baseline': [], 'candidate': []}
    report = dict(settings=vars(a).copy(), sha256={k: digest(paths[k]) for k in
                  ('harness', 'baseline', 'candidate', 'state', 'options')}, runs=[])
    report['settings'] = {k: str(v) if isinstance(v, Path) else v for k, v in report['settings'].items()}
    for block in range(a.blocks):
        for slot, name in enumerate(('baseline', 'candidate', 'candidate', 'baseline')):
            stem = paths['output']/f'{block}-{slot}-{name}'
            command = [str(paths['harness']), str(paths[name]), str(paths['system']),
                       str(paths['content']), '--frames', str(a.frames), '--load', '1',
                       '--load-every', str(a.segment), '--state-in', str(paths['state']),
                       '--csv', str(stem.with_suffix('.csv'))] + opts
            if a.renderer == 'vulkan':
                command += ['--vulkan']
            with stem.with_suffix('.log').open('w') as log:
                result = subprocess.run(command, cwd=paths['state'].parent, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=180)
            text = stem.with_suffix('.log').read_text(errors='replace')
            loads = re.findall(r'load at (\d+): ok=(\d)', text)
            expected = list(range(1, a.frames, a.segment))
            if result.returncode or loads != [(str(n), '1') for n in expected]:
                raise ValueError('failed/incomplete checkpoint loads: '+str(stem))
            if re.search(r'Assertion failed|Validation Error|FAIL:', text):
                raise ValueError('runtime validation failed: '+str(stem))
            value = timings(stem.with_suffix('.csv'), a.frames, a.segment, a.warmup)
            results[name].append(value)
            version = re.search(r'built from commit (\w+)', text)
            report['runs'].append(dict(block=block, slot=slot, build=name, mean_us=value,
                                       commit=version[1] if version else 'unknown'))
            print(f'{block}-{slot} {name}: {1000000/value:.2f} FPS', flush=True)
    report.update(assess(results['baseline'], results['candidate'], a.max_slowdown, a.max_noise))
    (paths['output']/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f"{report['status']}: {report['baseline_fps']:.2f} -> {report['candidate_fps']:.2f} FPS; "
          f"callback time {report['slowdown']:+.1%}")
    return {'pass': 0, 'regression': 1, 'inconclusive': 2}[report['status']]


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print('inconclusive:', error)
        raise SystemExit(2)

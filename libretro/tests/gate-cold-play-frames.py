"""Audit trusted-runner callback evidence, not full physical-GPU qualification.

Exit 0: this callback gate passes; 1: regression; 2: unqualified evidence.
Private assets and capture adapters are not part of this tool.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import heapq
import importlib.util
import json
import math
from pathlib import Path
import re
import statistics


SPEC = importlib.util.spec_from_file_location(
    'throughput_gate', Path(__file__).with_name('benchmark-core-throughput.py'))
THROUGHPUT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(THROUGHPUT)
RENDERERS = ('opengl', 'vulkan')
CACHES = ('cache-empty', 'warm-persistent')
DEADLINE_US = 16683
LIMIT = 0.05
FIELDS = set('schema commit reference kind revision renderer cache block slot file '
             'hardware options_sha256 binary_sha256 clean_core fresh_process '
             'overlapping_work image_readbacks image_allocations driver_cache case'.split())


def check(condition, message):
    if not condition:
        raise ValueError(message)


def object_unique(items):
    result = {}
    for key, value in items:
        check(key not in result, 'duplicate JSON key')
        result[key] = value
    return result


def load_json(value):
    def nonfinite(_):
        raise ValueError('nonfinite JSON')
    return json.loads(value, object_pairs_hook=object_unique,
                      parse_constant=nonfinite)


def digest_string(value, length):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{%d}' % length, value)


def integer(value, low, high):
    return type(value) is int and low <= value <= high


def frame_file(root, name):
    check(isinstance(name, str) and 0 < len(name) <= 200, 'invalid frame filename')
    part = Path(name)
    check(not part.is_absolute() and '..' not in part.parts and part.suffix == '.csv',
          'frame file must be a relative CSV within the public report')
    path = (root / part).resolve()
    check(path.is_relative_to(root), 'frame file escapes public report')
    check(path.is_file() and path.stat().st_size <= 32 * 1024 * 1024,
          'missing or oversized frame CSV')
    return path


def stats(samples):
    ordered = sorted(samples)
    return dict(count=len(samples), mean_us=statistics.mean(samples), max_us=ordered[-1],
                **{name: ordered[math.ceil(len(ordered) * q) - 1]
                   for name, q in (('p50_us', .5), ('p95_us', .95), ('p99_us', .99),
                                   ('p99_9_us', .999))})


def read_frames(path):
    # Stream bounded metadata records. Keep duration scalars for statistics,
    # never a second byte-for-byte log buffer or any shader/geometry payload.
    whole, fight, live, misses = [], [], [], []
    miss_count = 0
    progress = 0
    previous_end = -1
    saw_menu = saw_intro = False
    with path.open(encoding='ascii', newline='') as source:
        for index, line in enumerate(iter(lambda: source.readline(4097), '')):
            check(index < 50000 and len(line) <= 4096, 'oversized frame record/count')
            raw = next(csv.reader([line]))
            check(len(raw) == 11 and all(len(x) <= 20 for x in raw), 'frame CSV schema mismatch')
            row = [int(x) for x in raw]
            callback, audio, video, duration, start, state, scene, cursor, phase, life, count = row
            check(callback == index, 'missing, duplicated or reordered callback')
            check(0 < duration <= 3600000000 and start >= previous_end, 'invalid callback clock')
            previous_end = start + duration
            expected_audio = (callback + 1) * 4004 // 5 - callback * 4004 // 5
            check(audio == expected_audio and video in (1, 2), 'invalid audio cadence or skipped video')
            check(all(0 <= x <= 0xffffffff for x in (state, scene, cursor, phase, life)),
                  'invalid progression metadata')
            whole.append(duration)
            is_fight = state == 1 and scene == 10
            if not is_fight and state in (0, 2) and progress == 0:
                saw_menu = True
            if is_fight:
                fight.append(duration)
                if count == 0:
                    saw_intro = True
                if duration > DEADLINE_US:
                    miss_count += 1
                    # Count every hitch; retain bounded worst IDs in the
                    # summary. The published raw CSV retains every frame.
                    item = (duration, callback, count)
                    if len(misses) < 64:
                        heapq.heappush(misses, item)
                    else:
                        heapq.heappushpop(misses, item)
            check(count == progress or count == progress + 1, 'invalid live progression')
            if count != progress:
                check(saw_menu and saw_intro and is_fight and phase == 1 and count <= 600,
                      'not a fresh boot through intro and live fight')
                live.append(duration)
                progress = count
    check(progress == 600 and saw_menu and saw_intro and live and fight,
          'incomplete first-fight workload')
    return dict(whole=stats(whole), fight=stats(fight), live=stats(live), miss_count=miss_count,
                misses=[dict(callback=i, duration_us=d, live=n) for d, i, n in sorted(misses, reverse=True)])


def audit(root, revisions, accepted):
    root = Path(root).resolve()
    check(digest_string(accepted, 40), 'accepted reference must be a full pinned SHA')
    check(type(revisions) is dict and set(revisions) == {'include'} and
          type(revisions['include']) is list and 0 < len(revisions['include']) <= 64,
          'invalid revision matrix')
    pairs = set()
    for pair in revisions['include']:
        check(type(pair) is dict and set(pair) == {'commit', 'reference', 'kind'}, 'invalid revision pair')
        c, r, k = pair['commit'], pair['reference'], pair['kind']
        check(digest_string(c, 40) and digest_string(r, 40) and c != r and
              k in ('parent', 'accepted'), 'invalid revision identity')
        check((c, r, k) not in pairs, 'duplicate revision pair')
        check(k != 'accepted' or r == accepted, 'wrong accepted reference')
        pairs.add((c, r, k))
    for commit in {c for c, _, _ in pairs}:
        check(sum(c == commit and k == 'parent' for c, _, k in pairs) == 1,
              'each commit needs exactly one parent')
        check(any(c == commit and r == accepted for c, r, _ in pairs), 'missing accepted comparison')

    expected = {(c, r, k, backend, cache, block, slot)
                for c, r, k in pairs for backend in RENDERERS for cache in CACHES
                for block in range(2) for slot in range(4)}
    seen, files, options, binaries, hosts = {}, set(), {}, {}, set()
    manifest = root / 'callback-runs.jsonl'
    check(manifest.is_file() and manifest.resolve().is_relative_to(root),
          'missing or escaping callback-runs.jsonl')
    with manifest.open(encoding='utf-8') as source:
        for index, line in enumerate(iter(lambda: source.readline(16385), '')):
            check(index < 8192 and len(line) <= 16384, 'oversized manifest record/count')
            run = load_json(line)
            check(type(run) is dict and set(run) == FIELDS and type(run['schema']) is int and
                  run['schema'] == 1, 'unsupported callback manifest schema')
            check(integer(run['block'], 0, 1) and integer(run['slot'], 0, 3), 'invalid ABBA position')
            key = tuple(run[n] for n in ('commit', 'reference', 'kind', 'renderer', 'cache', 'block', 'slot'))
            check(key in expected and key not in seen, 'unexpected or duplicate run')
            revision = run['reference'] if run['slot'] in (0, 3) else run['commit']
            check(run['revision'] == revision and run['case'] == 'doa3-first-fights', 'wrong run identity/case')
            check(run['clean_core'] is True and run['fresh_process'] is True and
                  run['overlapping_work'] is False, 'instrumented, overlapping or reused-process timing')
            check(integer(run['image_readbacks'], 0, 0) and integer(run['image_allocations'], 0, 0),
                  'timing performed image readbacks/allocations')
            driver = 'isolated-empty' if run['cache'] == 'cache-empty' else 'warmed-controlled'
            check(run['driver_cache'] == driver, 'driver-cache state is not qualified')
            check(isinstance(run['hardware'], str) and re.fullmatch('[a-zA-Z0-9_.-]{1,128}', run['hardware']),
                  'missing/invalid hardware identity')
            hosts.add(run['hardware'])
            for name in ('options_sha256', 'binary_sha256'):
                check(digest_string(run[name], 64), 'missing/invalid ' + name)
            optkey = run['renderer']
            check(options.setdefault(optkey, run['options_sha256']) == run['options_sha256'], 'changed options')
            check(binaries.setdefault(revision, run['binary_sha256']) == run['binary_sha256'], 'changed binary')
            path = frame_file(root, run['file'])
            check(path not in files, 'reused frame file')
            files.add(path)
            seen[key] = dict(file=run['file'], **read_frames(path))
    check(set(seen) == expected and len(hosts) == 1, 'missing matrix runs or changed hardware')

    failures, comparisons = [], []
    for key, run in seen.items():
        # A hitch fails regardless of favourable averages or noisy timings.
        # Reference hitches also prevent blessing an already-regressed baseline.
        if run['miss_count']:
            failures.append(dict(run=list(key), file=run['file'], miss_count=run['miss_count'], misses=run['misses']))
    for c, r, kind in sorted(pairs):
        for backend in RENDERERS:
            for cache in CACHES:
                runs = [seen[c, r, kind, backend, cache, block, slot]
                        for block in range(2) for slot in range(4)]
                for scope in ('whole', 'fight', 'live'):
                    before = [run[scope]['mean_us'] for i, run in enumerate(runs) if i % 4 in (0, 3)]
                    after = [run[scope]['mean_us'] for i, run in enumerate(runs) if i % 4 in (1, 2)]
                    result = THROUGHPUT.assess(before, after, LIMIT, LIMIT)
                    result.pop('baseline_fps')
                    result.pop('candidate_fps')
                    comparisons.append(dict(commit=c, reference=r, kind=kind, renderer=backend,
                                            cache=cache, scope=scope, **result))
                before_max = max(run['whole']['max_us'] for i, run in enumerate(runs) if i % 4 in (0, 3))
                after_max = max(run['whole']['max_us'] for i, run in enumerate(runs) if i % 4 in (1, 2))
                if after_max > before_max * (1 + LIMIT):
                    failures.append(dict(commit=c, reference=r, renderer=backend, cache=cache,
                                         issue='new whole-boot maximum', before_us=before_max, after_us=after_max))
    status = ('regression' if failures or any(c['status'] == 'regression' for c in comparisons) else
              'unqualified' if any(c['status'] != 'pass' for c in comparisons) else 'pass')
    return dict(status=status, scope='callback gate only; not full GPU/presentation qualification',
                deadline_us=DEADLINE_US, noise_limit=LIMIT, slowdown_limit=LIMIT,
                failures=failures, comparisons=comparisons,
                runs=[dict(key=list(key), **run) for key, run in seen.items()])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--revisions-json', required=True)
    parser.add_argument('--accepted', required=True)
    args = parser.parse_args()
    root = args.report.resolve()
    try:
        result = audit(root, load_json(args.revisions_json), args.accepted)
    except (ValueError, OSError, TypeError, KeyError) as error:
        result = dict(status='unqualified', error=str(error))
    # Keep a bounded, metadata-only summary even when the evidence is rejected.
    root.mkdir(parents=True, exist_ok=True)
    with (root / 'callback-gate.json').open('w', encoding='utf-8') as output:
        json.dump(result, output, indent=2, allow_nan=False)
        output.write('\n')
    print(result['status'] + ': callback evidence gate')
    return {'pass': 0, 'regression': 1, 'unqualified': 2}[result['status']]


if __name__ == '__main__':
    raise SystemExit(main())

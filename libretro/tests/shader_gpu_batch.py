# SPDX-License-Identifier: GPL-2.0-or-later
"""One original numerical batch, bounded children and one shared watchdog."""
import os
import re
import subprocess
import tempfile
import time


def check_coverage_controls(executable):
    # Exercise the real C assertions, including families whose positive signal
    # need not appear in each individual child range.
    cases = [
        ('written', [4096, 4608, 4096, 4096, 4096, 0, 0, 0, 0, 0], 2),
        ('discarded', [4096, 4608, 4096, 4096, 4096, 0, 0, 0, 0, 0], 3),
        ('sampled', [16896, 17408, 8192, 0, 8192, 0, 0, 0, 0, 0], 4),
        ('point', [8192, 8704, 4096, 4096, 4096, 4096, 0, 0, 0, 0], 5),
        ('metadata', [74752, 75264, 8192, 0, 8192, 0, 1, 0, 0, 0], 6),
        ('sampler', [77824, 78336, 8192, 0, 8192, 0, 0, 1, 0, 0], 7),
        ('composition', [79360, 80656, 20736, 0, 20736, 0, 0, 0, 1, 0], 8),
        ('alias', [80656, 81168, 8192, 0, 8192, 0, 0, 0, 0, 1], 9),
        ('alias pixels', [80656, 81168, 8192, 0, 8192, 0, 0, 0, 0, 1], 2),
    ]
    for name, counts, changed in cases:
        command = [str(executable), '--verify-coverage']
        subprocess.run(command + list(map(str, counts)), check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
        broken = list(counts)
        broken[changed] = 0
        result = subprocess.run(command + list(map(str, broken)), capture_output=True,
                                text=True, timeout=10)
        assert result.returncode != 0 and 'Assertion' in result.stderr, name
        print('Required original coverage control rejected:', name, flush=True)


def coverage(text, first, limit):
    rows = re.findall(r'^PSH_COVERAGE ([0-9 ]+)$', text, re.MULTILINE)
    assert len(rows) == 1, 'missing or duplicate child coverage'
    values = list(map(int, rows[0].split()))
    assert len(values) == 10 and values[:2] == [first, limit], 'wrong child range'
    counts = values[2:]
    assert counts[0] + counts[1] == (limit - first) * 16, 'missing pixel coverage'
    assert counts[2] <= counts[0] and counts[3] <= counts[0]
    numerical = re.findall(r'Fragment GPU: (\d+) scalar color/depth/discard results, '
                           r'0 differences, 0 validation errors;', text)
    assert numerical == [str((limit - first) * 80)], 'missing numerical comparisons'
    return counts


def run_batch(executable, first, limit, workers):
    assert 0 <= first < limit <= 82704 and 1 <= workers <= 4
    workers = min(workers, limit - first)
    deadline = time.monotonic() + 300
    children = []
    totals = [0] * 8
    environment = dict(os.environ, LP_NUM_THREADS='1')
    print(f'Fragment batch [{first}, {limit}): {workers} independent workers, shared 300s deadline', flush=True)
    try:
        for index in range(workers):
            start = first + (limit - first) * index // workers
            end = first + (limit - first) * (index + 1) // workers
            stream = tempfile.TemporaryFile(mode='w+b')
            command = [str(executable), str(start), str(end), '--defer-coverage']
            try:
                child = subprocess.Popen(command, env=environment, stdout=stream, stderr=subprocess.STDOUT)
            except BaseException:
                stream.close()
                raise
            children.append((child, stream, command, start, end))
        for child, stream, command, start, end in children:
            result = child.wait(timeout=max(0, deadline - time.monotonic()))
            stream.seek(0)
            raw = stream.read(65537)
            text = raw.decode(errors='replace')
            print(text, end='', flush=True)
            assert len(raw) <= 65536, 'child log exceeded its bound'
            if result:
                raise subprocess.CalledProcessError(result, command)
            counts = coverage(text, start, end)
            totals = [a + b for a, b in zip(totals, counts)]
        # The original C coverage assertions run once on the original range.
        # Child ranges still execute every draw and exact numerical comparison.
        subprocess.run([str(executable), '--verify-coverage', str(first), str(limit),
                        *map(str, totals)], check=True, env=environment,
                       timeout=max(0, deadline - time.monotonic()))
        print(f'Original fragment batch [{first}, {limit}): {(limit-first)*80} scalar '
              'comparisons and original C coverage checks PASS', flush=True)
    finally:
        for child, stream, *_ in children:
            if child.poll() is None:
                child.kill()
        for child, stream, *_ in children:
            child.wait()
            stream.close()

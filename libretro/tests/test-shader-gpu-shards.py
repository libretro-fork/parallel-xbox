#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the public CLI's complete, disjoint fragment coverage without a GPU."""
from pathlib import Path
import json
import os
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
runner = Path(__file__).with_name('test-spv-psh-gpu.py')
environment = dict(os.environ)
environment.pop('SPV_PSH_GPU_RANGE', None)


def manifest(*arguments):
    return json.loads(subprocess.check_output(
        [sys.executable, str(runner), str(root), '--list-batches', *arguments],
        env=environment, text=True))


whole = manifest()
assert whole['deadline_seconds'] == 300 and whole['workers'] == 4
expected = whole['batches']
assert len(expected) == 160
assert [case for first, limit in expected for case in range(first, limit)] == list(range(82704))
seen = []
controls = 0
for index in range(8):
    part = manifest('--shard', f'{index}/8', '--workers', '4')
    assert part['shard'] == index and part['shards'] == 8
    assert part['workers'] == 4 and part['deadline_seconds'] == 300
    assert part['batches'] == expected[index::8]
    seen += part['batches']
    controls += part['composition_control']
assert sorted(seen) == sorted(expected) and len(seen) == len(expected)
assert controls == 1 and whole['composition_control']
for arguments in [('--shard', '8/8'), ('--shard', '0/0'), ('--shard', '-1/8'),
                  ('--shard', 'bad'), ('--workers', '0'), ('--workers', '5')]:
    result = subprocess.run([sys.executable, str(runner), str(root), '--list-batches', *arguments],
                            env=environment, capture_output=True, text=True)
    assert result.returncode == 2 and 'error:' in result.stderr
environment['SPV_PSH_GPU_RANGE'] = '27648:28160'
assert manifest()['batches'] == [[27648, 28160]]
result = subprocess.run([sys.executable, str(runner), str(root), '--list-batches', '--shard', '0/8'],
                        env=environment, capture_output=True, text=True)
assert result.returncode == 2 and 'cannot be combined' in result.stderr
print('Fragment shards: all 82704 cases, 160 unchanged batches, exact-once coverage/control, '
      '300s watchdog, bounded workers and invalid configuration rejection PASS')

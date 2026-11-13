#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual batch scheduling, shared deadlines, coverage and cleanup."""
import contextlib
import io
from unittest.mock import patch
import subprocess
import shader_gpu_batch as batch


class Clock:
    now = 10

    def __call__(self):
        return self.now


def exercise(fault=''):
    clock = Clock()
    children = []
    waits = []
    verified = []

    class Child:
        def __init__(self, command, env, stdout, stderr):
            self.command = command
            self.index = len(children)
            self.alive = True
            self.killed = False
            children.append(self)
            clock.now += 1
            first, limit = map(int, command[1:3])
            assert command[3] == '--defer-coverage' and env['LP_NUM_THREADS'] == '1'
            pixels = (limit - first) * 16
            if fault == 'range' and self.index == 0:
                first += 1
            text = f'Fragment GPU: {(limit-first)*80} scalar color/depth/discard results, 0 differences, 0 validation errors; validation=enabled\n'
            text += f'PSH_COVERAGE {first} {limit} {pixels} 0 {pixels} {pixels} 1 1 1 1\n'
            if fault == 'pixels':
                text = text.replace(f'{pixels} 0', '0 0')
            if fault == 'numeric':
                text = text.replace('0 differences', '1 differences')
            if self.index & 1:
                text = text.replace('\n', '\r\n')
            stdout.write(text.encode())
            stdout.flush()

        def wait(self, timeout=None):
            if timeout is not None:
                waits.append(timeout)
                assert len(children) == 4, 'a child was awaited before every child started'
                if fault == 'timeout' and self.index == 1:
                    raise subprocess.TimeoutExpired(self.command, timeout)
                clock.now += 10
                self.alive = False
            return 1 if fault == 'failure' and self.index == 0 else 0

        def poll(self):
            return None if self.alive else 0

        def kill(self):
            self.killed = True
            self.alive = False

    def verify(command, check, env, timeout):
        assert command == ['fixture', '--verify-coverage', '4096', '4608',
                           '8192', '0', '8192', '8192', '4', '4', '4', '4']
        assert timeout == 256 and check
        verified.append(True)
        if fault == 'coverage':
            raise subprocess.CalledProcessError(1, command)

    with patch.object(batch.time, 'monotonic', clock), \
         patch.object(batch.subprocess, 'Popen', Child), \
         patch.object(batch.subprocess, 'run', verify), \
         contextlib.redirect_stdout(io.StringIO()):
        try:
            batch.run_batch('fixture', 4096, 4608, 4)
        except (AssertionError, subprocess.SubprocessError):
            assert fault, 'positive scheduler failed'
        else:
            assert not fault, 'negative control escaped: '+fault
    assert all(not child.alive for child in children)
    if not fault:
        assert waits == [296, 286, 276, 266] and verified == [True]
        assert [(int(c.command[1]), int(c.command[2])) for c in children] == [
            (4096, 4224), (4224, 4352), (4352, 4480), (4480, 4608)]
    else:
        print('Required shared-batch control rejected:', fault)


exercise()
for fault in ['range', 'pixels', 'numeric', 'timeout', 'failure', 'coverage']:
    exercise(fault)
print('Shared GPU batch: exact partitions, one 300s deadline, original-range coverage '
      'verification and child cleanup PASS')

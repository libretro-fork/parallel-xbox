"""Ensure the throughput gate catches the observed DOA3 drop and rejects noise.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('gate', Path(__file__).with_name('benchmark-core-throughput.py'))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class ThroughputGate(unittest.TestCase):
    def test_observed_regression(self):
        # Repeated 600-run ABBA retail timings, before/after d12b464101.
        result = gate.assess([4900, 4920, 4910, 4900], [5980, 6000, 5990, 5980])
        self.assertEqual(result['status'], 'regression')

    def test_small_noise_passes(self):
        self.assertEqual(gate.assess([4900]*4, [4910, 4890, 4900, 4910])['status'], 'pass')

    def test_speedup_passes(self):
        self.assertEqual(gate.assess([5990]*4, [4900]*4)['status'], 'pass')

    def test_noisy_experiment_does_not_pass(self):
        self.assertEqual(gate.assess([4900, 5400, 4900, 4900], [4900]*4)['status'], 'inconclusive')

    def test_inconsistent_blocks_do_not_pass(self):
        self.assertEqual(gate.assess([5000]*4, [5390, 5390, 5490, 5490])['status'], 'inconclusive')

    def test_invalid_timings(self):
        for value in (0, -1, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                gate.assess([4900]*4, [value]*4)

    def test_warmup_and_reload_are_excluded(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)/'samples.csv'
            p.write_text(''.join(f'{i},800,1,{999999 if i == 0 or (i-1)%10 < 2 else 5000}\n'
                                 for i in range(30)))
            self.assertEqual(gate.timings(p, 30, 10, 2), 5000)
            with self.assertRaises(ValueError):
                gate.timings(p, 31, 10, 2)


if __name__ == '__main__':
    unittest.main()

"""Baseline A: I_A over whole publication intervals (active-wifi spec, Bench step 5)."""
import tempfile
import unittest
from pathlib import Path

import baseline
import runs
import synth

STEM = "20261121-120000_active-wifi_trace_r1"
# One 3 s publication interval: 100 ms at 120 mA, then 2.9 s at 20 mA. Mean 70/3 mA.
INTERVAL = [("00", 100, 120.0), ("00", 2900, 20.0)]
I_A_MA = 70 / 3


class BaselineRun(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def analyze(self, blocks):
        path = synth.write_run(self.dir, STEM, blocks, synth.sidecar(3))
        return baseline.analyze_baseline_run(runs.load_run(path))

    def test_mean_over_whole_intervals(self):
        result = self.analyze(INTERVAL * 5)
        self.assertEqual(result.n_intervals, 4)  # the first interval is discarded
        self.assertAlmostEqual(result.i_a_ma, I_A_MA, places=6)

    def test_energy_per_publication(self):
        result = self.analyze(INTERVAL * 5)
        self.assertAlmostEqual(result.energy_per_publication_mj, 350.0, places=6)  # 70/3 x 5 x 3

    def test_first_interval_and_trailing_partial_do_not_count(self):
        first = [("00", 3000, 200.0)]
        trailing = [("00", 1000, 200.0)]
        result = self.analyze(first + INTERVAL * 3 + trailing)
        self.assertEqual(result.n_intervals, 3)
        self.assertAlmostEqual(result.i_a_ma, I_A_MA, places=6)

    def test_interval_with_a_hole_is_reported_and_excluded(self):
        path = synth.write_run(self.dir, STEM, INTERVAL * 4, synth.sidecar(3))
        lines = path.read_text().splitlines()
        # 20 ms lost in the third interval; the hold would misstate its charge.
        start = 2 * 6500
        del lines[start + 1:start + 41]
        path.write_text("\n".join(lines) + "\n", newline="\n")
        result = baseline.analyze_baseline_run(runs.load_run(path))
        self.assertEqual(result.n_intervals, 2)
        self.assertEqual(result.excluded_intervals, [2])
        self.assertAlmostEqual(result.i_a_ma, I_A_MA, places=6)

    def test_too_short_a_run_has_no_result(self):
        result = self.analyze(INTERVAL)
        self.assertEqual(result.n_intervals, 0)
        self.assertIsNone(result.i_a_ma)
        self.assertIsNone(result.energy_per_publication_mj)


if __name__ == "__main__":
    unittest.main()

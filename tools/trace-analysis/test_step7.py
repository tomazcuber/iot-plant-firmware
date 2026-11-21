"""Step 7 model validation on synthetic 60 s / 300 s conditions."""
import tempfile
import unittest
from pathlib import Path

import baseline_a
import cycles
import runs
import step7
import summary
import synth

I_SLEEP_MA = 0.5


def main_condition(directory, shape, interval_s, run=1):
    path = synth.write_main_run(Path(directory), [shape] * 2, run=run, interval_s=interval_s)
    return summary.MainCondition([cycles.analyze_main_run(runs.load_run(path))])


def baseline_a_condition(directory, interval_s, ma):
    stem = f"20261121-1300{interval_s:02d}_active-wifi_trace_r1"
    path = synth.write_run(Path(directory), stem, [("00", interval_s * 3000, ma)],
                           synth.sidecar(interval_s))
    return summary.BaselineACondition([baseline_a.analyze_baseline_a_run(runs.load_run(path))])


def bursty_baseline_a_condition(directory, interval_s):
    stem = f"20261121-1400{interval_s:02d}_active-wifi_trace_r1"
    blocks = [("00", 100, 120.0), ("00", interval_s * 1000 - 100, 20.0)] * 3
    path = synth.write_run(Path(directory), stem, blocks, synth.sidecar(interval_s))
    return summary.BaselineACondition([baseline_a.analyze_baseline_a_run(runs.load_run(path))])


class Pair60300(unittest.TestCase):
    """The acceptance pair, at the real intervals."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        d = cls.tmp.name
        cls.at_60 = main_condition(d, synth.CycleShape(sleep_s=60), 60, run=1)
        cls.at_300 = main_condition(d, synth.CycleShape(sleep_s=300), 300, run=2)
        cls.hot_60 = main_condition(d, synth.CycleShape(sleep_s=60, wifi_ma=150.0), 60, run=3)

    def test_same_phases_validate_with_near_zero_error_both_ways(self):
        result = step7.validate(self.at_60, self.at_300, I_SLEEP_MA)
        self.assertTrue(result.passed)
        self.assertEqual([(d.fit_interval_s, d.target_interval_s) for d in result.directions],
                         [(60, 300), (300, 60)])
        for direction in result.directions:
            self.assertAlmostEqual(direction.error_pct, 0.0, places=6)
        self.assertAlmostEqual(result.directions[0].e_measured_mj, 1785.625, places=6)

    def test_a_different_active_phase_is_flagged_both_ways(self):
        result = step7.validate(self.hot_60, self.at_300, I_SLEEP_MA)
        self.assertFalse(result.passed)
        forward, reverse = result.directions
        # Active charge 252.125 mA s at 60 s and 207.125 at 300 s; sleep 0.5 mA.
        self.assertAlmostEqual(forward.e_predicted_mj, 2010.625, places=6)
        self.assertAlmostEqual(forward.e_measured_mj, 1785.625, places=6)
        self.assertAlmostEqual(forward.error_pct, 12.6006, places=3)
        self.assertAlmostEqual(reverse.e_predicted_mj, 1185.625, places=6)
        self.assertAlmostEqual(reverse.e_measured_mj, 1410.625, places=6)
        self.assertAlmostEqual(reverse.error_pct, -15.9504, places=3)
        self.assertFalse(forward.passed)
        self.assertFalse(reverse.passed)


class SmallPair(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.d = self.tmp.name

    def test_reports_mains_floor_next_to_i_sleep_and_flags_a_mismatch(self):
        fit = main_condition(self.d, synth.CycleShape(sleep_s=6), 6, run=1)
        target = main_condition(self.d, synth.CycleShape(sleep_s=30, sleep_ma=0.8), 30, run=2)
        forward = step7.validate(fit, target, I_SLEEP_MA).directions[0]
        self.assertAlmostEqual(forward.i_sleep_ma, 0.5)
        self.assertAlmostEqual(forward.floor_ma, 0.8, places=6)
        self.assertFalse(forward.floor_matches)
        # (0.8 - 0.5) mA x 30 s x 5 V = 45 mJ of a measured (207.125 + 24) x 5 mJ
        self.assertAlmostEqual(forward.floor_effect_pct, 45 / 1155.625 * 100, places=4)

    def test_a_floor_within_one_lsb_matches(self):
        fit = main_condition(self.d, synth.CycleShape(sleep_s=6), 6, run=1)
        target = main_condition(self.d, synth.CycleShape(sleep_s=30), 30, run=2)
        forward = step7.validate(fit, target, 0.45).directions[0]
        self.assertTrue(forward.floor_matches)

    def test_reports_nominal_and_measured_sleep_time(self):
        fit = main_condition(self.d, synth.CycleShape(sleep_s=6), 6, run=1)
        target = main_condition(self.d, synth.CycleShape(sleep_s=31), 30, run=2)
        forward = step7.validate(fit, target, I_SLEEP_MA).directions[0]
        self.assertAlmostEqual(forward.t_sleep_measured_s, 31.0, delta=0.0005)
        self.assertEqual(forward.t_sleep_nominal_s, 30)

    def test_break_even_sits_next_to_a_passing_error(self):
        fit = main_condition(self.d, synth.CycleShape(sleep_s=6), 6, run=1)
        target = main_condition(self.d, synth.CycleShape(sleep_s=30), 30, run=2)
        baselines_a = {6: baseline_a_condition(self.d, 6, 30.0),
                     30: baseline_a_condition(self.d, 30, 30.0)}
        result = step7.validate(fit, target, I_SLEEP_MA, baselines_a=baselines_a)
        self.assertTrue(result.passed)
        for direction in result.directions:
            # 207.125 mA s active / (30 - 0.5) mA
            self.assertAlmostEqual(direction.break_even_s, 7.02119, places=4)
            self.assertIsNone(direction.break_even_withheld)

    def test_break_even_accounts_for_baseline_a_publication_cost(self):
        fit = main_condition(self.d, synth.CycleShape(sleep_s=6), 6, run=1)
        target = main_condition(self.d, synth.CycleShape(sleep_s=30), 30, run=2)
        # Each publication: 100 ms at 120 mA, idle at 20 mA. E_A(T) = 50 + 100 T mJ.
        baselines_a = {t: bursty_baseline_a_condition(self.d, t) for t in (6, 30)}
        result = step7.validate(fit, target, I_SLEEP_MA, baselines_a=baselines_a)
        for direction in result.directions:
            # (207.125 x 5 - 50) / (100 - 0.5 x 5)
            self.assertAlmostEqual(direction.break_even_s, 10.10897, places=4)

    def test_break_even_needs_baseline_a_at_both_intervals(self):
        fit = main_condition(self.d, synth.CycleShape(sleep_s=6), 6, run=1)
        target = main_condition(self.d, synth.CycleShape(sleep_s=30), 30, run=2)
        baselines_a = {6: baseline_a_condition(self.d, 6, 30.0)}
        for direction in step7.validate(fit, target, I_SLEEP_MA, baselines_a=baselines_a).directions:
            self.assertIsNone(direction.break_even_s)
            self.assertIn("both intervals", direction.break_even_withheld)

    def test_break_even_is_withheld_when_step_7_fails(self):
        fit = main_condition(self.d, synth.CycleShape(sleep_s=6, wifi_ma=150.0), 6, run=1)
        target = main_condition(self.d, synth.CycleShape(sleep_s=30), 30, run=2)
        baselines_a = {6: baseline_a_condition(self.d, 6, 30.0),
                     30: baseline_a_condition(self.d, 30, 30.0)}
        result = step7.validate(fit, target, I_SLEEP_MA, baselines_a=baselines_a)
        self.assertFalse(result.passed)
        for direction in result.directions:
            self.assertIsNone(direction.break_even_s)
            self.assertIn("Step 7", direction.break_even_withheld)

    def test_no_baseline_a_means_no_break_even(self):
        fit = main_condition(self.d, synth.CycleShape(sleep_s=6), 6, run=1)
        target = main_condition(self.d, synth.CycleShape(sleep_s=30), 30, run=2)
        forward = step7.validate(fit, target, I_SLEEP_MA).directions[0]
        self.assertIsNone(forward.break_even_s)
        self.assertIn("baseline", forward.break_even_withheld)


class ConditionStatistics(unittest.TestCase):
    def test_mean_sd_and_n_over_cycles_and_runs(self):
        with tempfile.TemporaryDirectory() as d:
            results = []
            for run, wifi_ms in ((1, 1000), (2, 2000)):
                shapes = [synth.CycleShape(sleep_s=3, wifi_ms=wifi_ms)] * 3
                path = synth.write_main_run(Path(d), shapes, run=run, interval_s=3)
                results.append(cycles.analyze_main_run(runs.load_run(path)))
            rows = {r["metric"]: r for r in summary.MainCondition(results).rows()}
        wifi = rows["t_wifi_s"]
        self.assertEqual((wifi["cycle_n"], wifi["run_n"]), (4, 2))
        self.assertAlmostEqual(wifi["cycle_mean"], 1.5, places=6)
        self.assertAlmostEqual(wifi["cycle_sd"], 0.57735, places=4)  # sample SD of 1,1,2,2
        self.assertAlmostEqual(wifi["run_mean"], 1.5, places=6)
        self.assertAlmostEqual(wifi["run_sd"], 0.70711, places=4)    # sample SD of 1,2


if __name__ == "__main__":
    unittest.main()

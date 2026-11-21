"""Main Run analysis on synthetic traces with known phases.

Run: python -m unittest discover -s tools/trace-analysis
"""
import tempfile
import unittest
from pathlib import Path

import cycles
import runs
import synth


class CleanCycle(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        shape = synth.CycleShape(sleep_s=10)
        path = synth.write_main_run(Path(self.tmp.name), [shape] * 3, interval_s=10)
        self.result = cycles.analyze_main_run(runs.load_run(path))

    def test_first_cycle_is_discarded_and_the_others_kept(self):
        self.assertEqual([c.reason for c in self.result.cycles],
                         ["first_cycle", None, None, "incomplete"])
        self.assertEqual(len(self.result.included), 2)

    def test_recovers_each_duration_within_one_sample(self):
        expected = {"boot": 0.300, "sensor": 0.200, "wifi": 1.500, "teardown": 0.100,
                    "residual": 0.005, "sleep": 10.0}
        for cycle in self.result.included:
            for phase, seconds in expected.items():
                self.assertAlmostEqual(cycle.duration_s(phase), seconds, delta=0.0005, msg=phase)

    def test_recovers_each_current(self):
        expected = {"boot": 40.0, "sensor": 30.0, "wifi": 120.0, "teardown": 90.0,
                    "residual": 25.0, "sleep": 0.5}
        for cycle in self.result.included:
            for phase, ma in expected.items():
                self.assertAlmostEqual(cycle.current_ma(phase), ma, places=6, msg=phase)

    def test_recovers_cycle_energy(self):
        # 5 V x (40x0.3 + 30x0.2 + 120x1.5 + 90x0.1 + 25x0.005 + 0.5x10) mA s
        for cycle in self.result.included:
            self.assertAlmostEqual(cycle.e_cycle_mj, 1060.625, places=6)


SHAPE = synth.CycleShape(sleep_s=3)
GOOD_E_MJ = 5 * (12 + 6 + 180 + 9 + 0.125 + 1.5)  # 1043.125 mJ
BOOT = ("00", 300, 40.0)
SENSOR = ("01", 200, 30.0)
WIFI = ("10", 1500, 120.0)
TEARDOWN = ("11", 100, 90.0)
RESIDUAL = ("00", 5, 25.0)
SLEEP = ("00", 3000, 0.5)


class RunBuilder(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def analyze_with_middle(self, middle, stem="20261121-120000_main_trace_r1", interval_s=3):
        """A first good Cycle, the Cycle under test, a good Cycle, a closing wake."""
        blocks = [("00", 1000, 0.5)] + SHAPE.blocks() + middle + SHAPE.blocks() \
            + SHAPE.active_blocks() + [("00", 1000, 0.5)]
        path = synth.write_run(self.dir, stem, blocks, synth.sidecar(interval_s))
        return cycles.analyze_main_run(runs.load_run(path))


class UnexpectedSequences(RunBuilder):
    CASES = {
        "missing_wifi": ([BOOT, SENSOR, TEARDOWN, RESIDUAL, SLEEP],
                         ("00", "01", "11", "00")),
        "missing_teardown": ([BOOT, SENSOR, WIFI, RESIDUAL, SLEEP],
                             ("00", "01", "10", "00")),
        "wifi_back_to_sensor": ([BOOT, SENSOR, WIFI, SENSOR, WIFI, TEARDOWN, RESIDUAL, SLEEP],
                                ("00", "01", "10", "01", "10", "11", "00")),
        "teardown_to_sensor": ([BOOT, SENSOR, WIFI, TEARDOWN, SENSOR, RESIDUAL, SLEEP],
                               ("00", "01", "10", "11", "01", "00")),
        "one_sample_glitch": ([BOOT, SENSOR, ("10", 700, 120.0), ("00", 0.5, 120.0),
                               ("10", 799.5, 120.0), TEARDOWN, RESIDUAL, SLEEP],
                              ("00", "01", "10", "00", "10", "11", "00")),
        "no_markers": ([("00", 2100, 60.0), SLEEP], ("00",)),
    }

    def test_each_is_reported_with_its_sequence_and_excluded(self):
        for name, (middle, sequence) in self.CASES.items():
            with self.subTest(name):
                result = self.analyze_with_middle(middle)
                bad = result.cycles[1]
                self.assertEqual(bad.reason, "unexpected_sequence")
                self.assertEqual(bad.sequence, sequence)
                self.assertGreater(bad.start_us, 0)
                self.assertNotIn(bad, result.included)
                self.assertEqual([round(c.e_cycle_mj, 3) for c in result.included],
                                 [round(GOOD_E_MJ, 3)])


class Segmentation(RunBuilder):
    def test_a_spike_on_the_sleep_floor_is_not_a_rise(self):
        middle = SHAPE.active_blocks() + [("00", 1500, 0.5), ("00", 1, 50.0), ("00", 1499, 0.5)]
        result = self.analyze_with_middle(middle)
        self.assertEqual([c.reason for c in result.cycles],
                         ["first_cycle", None, None, "incomplete"])
        spiked = result.cycles[1]
        # The spike stays in the Cycle's energy: 1 ms at 49.5 mA above the floor.
        self.assertAlmostEqual(spiked.e_cycle_mj, GOOD_E_MJ + 5 * 0.0495, places=6)

    def test_a_dip_during_boot_does_not_split_the_cycle(self):
        middle = [("00", 100, 40.0), ("00", 3, 0.5), ("00", 197, 40.0)] \
            + SHAPE.active_blocks()[1:] + [SLEEP]
        result = self.analyze_with_middle(middle)
        self.assertIsNone(result.cycles[1].reason)
        self.assertAlmostEqual(result.cycles[1].duration_s("boot"), 0.300, delta=0.0005)

    def test_a_cut_short_sleep_and_the_alert_cycle_after_it_are_excluded(self):
        middle = SHAPE.active_blocks() + [("00", 1500, 0.5)]
        result = self.analyze_with_middle(middle)
        self.assertEqual([c.reason for c in result.cycles],
                         ["first_cycle", "sleep_off_interval", "after_interrupted_sleep",
                          "incomplete"])

    def test_an_alert_shortly_before_the_timer_is_still_excluded(self):
        # 2.8 s of a 3 s sleep is inside 10 % of nominal, but far outside timer jitter.
        middle = SHAPE.active_blocks() + [("00", 2800, 0.5)]
        result = self.analyze_with_middle(middle)
        self.assertEqual([c.reason for c in result.cycles],
                         ["first_cycle", "sleep_off_interval", "after_interrupted_sleep",
                          "incomplete"])

    def test_ordinary_timer_jitter_is_kept(self):
        middle = SHAPE.active_blocks() + [("00", 3050, 0.5)]
        result = self.analyze_with_middle(middle)
        self.assertEqual([c.reason for c in result.cycles],
                         ["first_cycle", None, None, "incomplete"])

    def test_a_hole_in_the_samples_excludes_the_cycle(self):
        blocks = [("00", 1000, 0.5)] + SHAPE.blocks() * 3 + SHAPE.active_blocks() \
            + [("00", 1000, 0.5)]
        path = synth.write_run(self.dir, "20261121-120000_main_trace_r1", blocks,
                               synth.sidecar(3))
        # 20 ms lost during the second Cycle's WiFi phase.
        first = round((1000 + 3 * 1000 + 2105 + 500 + 1000) * 2)
        lines = path.read_text().splitlines()
        del lines[first + 1:first + 41]
        path.write_text("\n".join(lines) + "\n", newline="\n")
        result = cycles.analyze_main_run(runs.load_run(path))
        self.assertEqual(result.cycles[1].reason, "sample_gap")
        self.assertEqual(result.cycles[1].seq_missing, 40)
        self.assertEqual(result.seq_missing, 40)

    def test_a_run_that_starts_awake_has_no_rise_for_its_first_stretch(self):
        blocks = SHAPE.blocks() * 3 + SHAPE.active_blocks() + [("00", 1000, 0.5)]
        path = synth.write_run(self.dir, "20261121-120000_main_trace_r1", blocks,
                               synth.sidecar(3))
        result = cycles.analyze_main_run(runs.load_run(path))
        self.assertEqual([c.reason for c in result.cycles],
                         ["incomplete", "first_cycle", None, "incomplete"])


class LoadRun(RunBuilder):
    def write(self, side):
        return synth.write_run(self.dir, "20261121-120000_main_trace_r1",
                               [("00", 10, 0.5)], side)

    def test_uses_the_r_eff_and_offset_in_the_sidecar(self):
        path = self.write(synth.sidecar(60, r_eff_ohm=0.2, offset_lsb=-4.0))
        # The file holds raw 3; (3 + 4) x 0.01 mV / 0.2 ohm = 0.35 mA.
        _, ma, _, _ = next(iter(runs.SampleReader(runs.load_run(path))))
        self.assertAlmostEqual(ma, 0.35)

    def test_skips_what_it_cannot_analyse(self):
        cases = {
            "no sidecar": None,
            "zero run": synth.sidecar(60, zero=True),
            "not trace": synth.sidecar(60, config="averaged"),
            "read_interval_s": synth.sidecar(None),
        }
        for needle, side in cases.items():
            with self.subTest(needle):
                self.dir.joinpath("20261121-120000_main_trace_r1.json").unlink(missing_ok=True)
                path = self.write(side)
                with self.assertRaisesRegex(runs.RunSkipped, needle):
                    runs.load_run(path)

    def test_counts_malformed_lines(self):
        path = self.write(synth.sidecar(60))
        with open(path, "a", encoding="ascii", newline="\n") as f:
            f.write("21,10500,3\n")
        reader = runs.SampleReader(runs.load_run(path))
        self.assertEqual(len(list(reader)), 21)
        self.assertEqual(reader.malformed, 1)

    def test_warns_when_the_session_had_no_zero_run(self):
        path = self.write(synth.sidecar(60, offset_source="calibration.json"))
        self.assertTrue(any("zero run" in w for w in runs.load_run(path).warnings))


if __name__ == "__main__":
    unittest.main()

"""The command line: from Run files to the per-condition summaries."""
import contextlib
import csv
import io
import json
import tempfile
import unittest
from pathlib import Path

import analyze
import synth

BAD_CYCLE = [("00", 300, 40.0), ("01", 200, 30.0), ("11", 100, 90.0), ("00", 5, 25.0),
             ("00", 6000, 0.5)]


def write_campaign(directory, wifi_ma_at_30=120.0):
    """main at 6 s (with one bad Cycle) and 30 s, baseline A at both, plus files to skip."""
    good = synth.CycleShape(sleep_s=6)
    blocks = [("00", 1000, 0.5)] + good.blocks() + good.blocks() + BAD_CYCLE + good.blocks() \
        + good.active_blocks() + [("00", 1000, 0.5)]
    synth.write_run(directory, "20261121-120001_main_trace_r1", blocks, synth.sidecar(6))
    synth.write_main_run(directory, [synth.CycleShape(sleep_s=30, wifi_ma=wifi_ma_at_30)] * 2,
                         run=2, interval_s=30)
    for interval in (6, 30):
        synth.write_run(directory, f"20261121-1300{interval:02d}_active-wifi_trace_r1",
                        [("00", interval * 3000, 30.0)], synth.sidecar(interval))
    synth.write_run(directory, "20261121-110000_acceptance_zero_r1", [("00", 10, 0.0)],
                    synth.sidecar(None, zero=True))
    synth.write_run(directory, "20261121-140000_main_trace_r9", [("00", 10, 0.5)], None)


class Cli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.data = Path(self.tmp.name, "data")
        self.out = Path(self.tmp.name, "out")
        self.data.mkdir()

    def run_cli(self, *extra):
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            code = analyze.main([str(self.data), "--out", str(self.out), *extra])
        return code, stdout.getvalue()

    def load(self, name):
        return json.loads((self.out / name).read_text(encoding="utf8"))

    def test_writes_one_summary_per_condition(self):
        write_campaign(self.data)
        code, _ = self.run_cli("--i-sleep-ma", "0.5")
        self.assertEqual(code, 0)
        for condition in ("main_6s", "main_30s", "active-wifi_6s", "active-wifi_30s"):
            self.assertTrue((self.out / f"{condition}.json").exists(), condition)
            self.assertTrue((self.out / f"{condition}.csv").exists(), condition)
        with open(self.out / "main_6s.csv", newline="", encoding="utf8") as f:
            rows = {r["metric"]: r for r in csv.DictReader(f)}
        self.assertAlmostEqual(float(rows["t_wifi_s"]["cycle_mean"]), 1.5, delta=0.0005)
        self.assertEqual(rows["E_cycle_mJ"]["cycle_n"], "2")
        self.assertEqual(rows["E_cycle_mJ"]["run_n"], "1")

    def test_excluded_cycles_are_reported_and_left_out_of_the_means(self):
        write_campaign(self.data)
        _, stdout = self.run_cli("--i-sleep-ma", "0.5")
        summary = self.load("main_6s.json")
        bad = [e for e in summary["excluded"] if e["reason"] == "unexpected_sequence"]
        self.assertEqual(len(bad), 1)
        self.assertEqual(bad[0]["sequence"], "00>01>11>00")
        self.assertEqual(bad[0]["run"], "20261121-120001_main_trace_r1")
        self.assertIn("unexpected_sequence", stdout)
        energy = next(m for m in summary["metrics"] if m["metric"] == "E_cycle_mJ")
        self.assertAlmostEqual(energy["cycle_mean"], 5 * (207.125 + 3), places=6)
        self.assertAlmostEqual(energy["cycle_sd"], 0.0, places=6)

    def test_per_cycle_csv_lists_every_cycle_with_its_status(self):
        write_campaign(self.data)
        self.run_cli("--i-sleep-ma", "0.5")
        with open(self.out / "main_6s_cycles.csv", newline="", encoding="utf8") as f:
            rows = list(csv.DictReader(f))
        self.assertEqual([r["reason"] for r in rows],
                         ["first_cycle", "", "unexpected_sequence", "", "incomplete"])
        self.assertAlmostEqual(float(rows[1]["t_sensor_s"]), 0.2, delta=0.0005)
        self.assertAlmostEqual(float(rows[1]["t_wifi_s"]), 1.5, delta=0.0005)
        self.assertEqual(rows[2]["t_wifi_s"], "")

    def test_skipped_files_are_reported_with_their_reason(self):
        write_campaign(self.data)
        _, stdout = self.run_cli("--i-sleep-ma", "0.5")
        skipped = self.load("report.json")["skipped"]
        self.assertIn("no sidecar", skipped["20261121-140000_main_trace_r9"])
        self.assertIn("zero run", skipped["20261121-110000_acceptance_zero_r1"])
        self.assertIn("no sidecar", stdout)

    def test_step_7_report_holds_both_directions_with_floor_and_break_even(self):
        write_campaign(self.data)
        self.run_cli("--i-sleep-ma", "0.5")
        report = self.load("step7.json")
        self.assertEqual(report["status"], "passed")
        pair = report["pairs"][0]
        self.assertEqual([(d["fit_interval_s"], d["target_interval_s"]) for d in pair["directions"]],
                         [(6, 30), (30, 6)])
        for direction in pair["directions"]:
            self.assertAlmostEqual(direction["error_pct"], 0.0, places=6)
            self.assertAlmostEqual(direction["floor_ma"], 0.5, places=6)
            self.assertAlmostEqual(direction["i_sleep_ma"], 0.5)
            self.assertAlmostEqual(direction["break_even_s"], 7.02119, places=4)

    def test_a_failed_step_7_is_flagged_in_the_exit_code(self):
        write_campaign(self.data, wifi_ma_at_30=150.0)
        code, stdout = self.run_cli("--i-sleep-ma", "0.5")
        self.assertEqual(code, 1)
        self.assertEqual(self.load("step7.json")["status"], "failed")
        self.assertIn("FAILED", stdout)
        for direction in self.load("step7.json")["pairs"][0]["directions"]:
            self.assertIsNone(direction["break_even_s"])

    def test_step_7_says_why_it_did_not_run(self):
        write_campaign(self.data)
        self.run_cli()
        report = self.load("step7.json")
        self.assertEqual(report["status"], "not_run")
        self.assertIn("--i-sleep-ma", report["reason"])

    def test_no_analysable_run_is_an_error(self):
        code, _ = self.run_cli("--i-sleep-ma", "0.5")
        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()

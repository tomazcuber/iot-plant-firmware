"""Tests for the recorder's pure logic (parsing, stats, naming, sidecar, run sequence).

Run: python -m unittest discover -s tools/ina219-reader/host
"""
import json
import os
import tempfile
import unittest
from datetime import datetime
from pathlib import Path

import record

# A simulated stream: seq 3..4 missing (gap of 2), one overflow report, markers
# change, a long (>= 532 us) interval between 6 and 7.
STREAM = [
    "# started trace 0x199D",
    "0,0,-3,00",
    "1,500,-2,00",
    "2,1000,-3,01",
    "5,2500,-4,10",
    "# ovf 2",
    "6,3000,-3,11",
    "7,3600,-3,00",
    "# stopped n=8 ovf=2",
]


class ParseLine(unittest.TestCase):
    def test_data_line(self):
        self.assertEqual(record.parse_data_line("1048,524000,-3,00"), (1048, 524000, -3, "00"))

    def test_comment_is_not_data(self):
        self.assertIsNone(record.parse_data_line("# ovf 3"))

    def test_garbage_is_not_data(self):
        for bad in ["", "1,2,3", "a,b,c,d", "1,2,3,4", "1,2,3,0x", "1,2,3,000"]:
            self.assertIsNone(record.parse_data_line(bad), bad)

    def test_ovf_line(self):
        self.assertEqual(record.parse_ovf("# ovf 12"), 12)
        self.assertIsNone(record.parse_ovf("# started trace 0x199D"))

    def test_stopped_line(self):
        self.assertEqual(record.parse_stopped("# stopped n=9 ovf=4"), (9, 4))
        self.assertIsNone(record.parse_stopped("# ovf 4"))

    def test_vbus_line(self):
        self.assertEqual(record.parse_vbus("# vbus_mV 4980"), 4980)
        self.assertIsNone(record.parse_vbus("# err busy"))


class Stats(unittest.TestCase):
    def feed(self, lines):
        s = record.StreamStats()
        for ln in lines:
            s.add_line(ln)
        return s

    def test_counts_gaps_and_overflows(self):
        s = self.feed(STREAM)
        self.assertEqual(s.samples, 6)
        self.assertEqual(s.seq_gaps, 1)       # one discontinuity...
        self.assertEqual(s.seq_missing, 2)    # ...of two missing samples
        self.assertEqual(s.overflows, 2)      # from the final "# stopped"
        self.assertEqual(s.reader_samples, 8)

    def test_interval_statistics(self):
        s = self.feed(STREAM)
        # intervals: 500, 500, 1500, 500, 600
        self.assertEqual(s.interval_count, 5)
        self.assertAlmostEqual(s.interval_mean_us, 720.0)
        self.assertEqual(s.interval_max_us, 1500)
        self.assertEqual(s.intervals_ge_532, 2)
        self.assertAlmostEqual(s.share_ge_532, 0.4)

    def test_interval_across_gap_still_counted(self):
        # The interval over a seq gap is a real elapsed time and must show up
        # in the >= 532 us share (it is the symptom of the gap).
        s = self.feed(["0,0,1,00", "2,1000,1,00"])
        self.assertEqual(s.interval_count, 1)
        self.assertEqual(s.intervals_ge_532, 1)

    def test_clean_stream(self):
        s = self.feed(["0,0,1,00", "1,500,1,00", "2,1000,1,00"])
        self.assertEqual((s.seq_gaps, s.seq_missing, s.overflows), (0, 0, 0))
        self.assertEqual(s.share_ge_532, 0.0)

    def test_empty(self):
        s = record.StreamStats()
        self.assertEqual(s.samples, 0)
        self.assertEqual(s.interval_mean_us, 0.0)
        self.assertEqual(s.share_ge_532, 0.0)

    def test_ovf_without_stopped_uses_last_report(self):
        s = self.feed(["0,0,1,00", "# ovf 5"])
        self.assertEqual(s.overflows, 5)

    def test_shunt_mean(self):
        s = self.feed(["0,0,2,00", "1,500,4,00"])
        self.assertEqual(s.shunt_mean, 3.0)

    def test_summary_text_mentions_everything(self):
        text = record.format_summary(self.feed(STREAM))
        for needle in ["samples", "gaps", "overflow", "mean interval", "532"]:
            self.assertIn(needle, text)


class Naming(unittest.TestCase):
    def test_stem(self):
        t = datetime(2026, 11, 3, 21, 5, 9)
        self.assertEqual(
            record.run_stem(t, "net-smoke", None, "trace", 3),
            "20261103-210509_net-smoke_trace_r3",
        )
        self.assertEqual(
            record.run_stem(t, "net-smoke", "none", "trace", 3),
            "20261103-210509_net-smoke_none_r3",
        )

    def test_out_dir_precedence(self):
        self.assertEqual(record.resolve_out_dir("X", {"INA219_DATA_DIR": "Y"}), Path("X"))
        self.assertEqual(record.resolve_out_dir(None, {"INA219_DATA_DIR": "Y"}), Path("Y"))
        self.assertEqual(record.resolve_out_dir(None, {}), Path(r"C:\zw\bench-data"))


class Calibration(unittest.TestCase):
    def test_committed_file_is_nominal_uncalibrated(self):
        cal = record.load_calibration()
        self.assertEqual(cal["r_eff_ohm"], 0.1)
        self.assertIs(cal["calibrated"], False)

    def test_current_ma(self):
        # 0.01 mV / 0.1 ohm = 0.1 mA per LSB
        self.assertAlmostEqual(record.shunt_raw_to_ma(-3, 0.1), -0.3)
        self.assertAlmostEqual(record.shunt_raw_to_ma(1000, 0.1), 100.0)


class FakeReader:
    """Stands in for the serial port. Replies are released by the commands sent;
    when the scripted data is used up, readline raises KeyboardInterrupt (Ctrl-C)."""

    def __init__(self, id_line, vbus_lines, data, stopped_line):
        self.id_line = id_line
        self.vbus = list(vbus_lines)
        self.data = list(data)
        self.stopped_line = stopped_line
        self.sent = []
        self.buf = []
        self.running = False

    def send(self, cmd):
        self.sent.append(cmd)
        if cmd == "id":
            self.buf.append(self.id_line)
        elif cmd == "vbus":
            self.buf.append(self.vbus.pop(0))
        elif cmd.startswith("start"):
            self.running = True
            self.buf.append("# started trace 0x199D")
            self.buf.extend(self.data)
        elif cmd == "stop":
            self.running = False
            self.buf.append(self.stopped_line)

    def readline(self):
        if self.buf:
            ln = self.buf.pop(0)
            if self.running and ln is not None and not self.buf:
                self.buf.append(None)  # data exhausted: next read is Ctrl-C
            return ln
        if self.running:
            raise KeyboardInterrupt
        return None


class RunSequence(unittest.TestCase):
    def test_full_run_writes_csv_and_sidecar(self):
        port = FakeReader("# id v0.1 2026-11-03", ["# vbus_mV 4990", "# vbus_mV 4985"],
                          STREAM[1:-1], STREAM[-1])
        # FakeReader appends None to signal "ctrl-c next"; handle in readline.
        with tempfile.TemporaryDirectory() as d:
            args = record.parse_args([
                "--port", "COM9", "--config", "trace", "--app", "net-smoke",
                "--run", "1", "--out", d, "--dut-commit", "abc123", "--note", "hi",
            ])
            summary = record.run(port, args, now=datetime(2026, 11, 3, 21, 0, 0))
            stem = "20261103-210000_net-smoke_trace_r1"
            csv_lines = Path(d, stem + ".csv").read_text().splitlines()
            side = json.loads(Path(d, stem + ".json").read_text())

        self.assertEqual(port.sent[:3], ["id", "vbus", "start trace"])
        self.assertEqual(port.sent[-2:], ["stop", "vbus"])
        self.assertEqual(csv_lines[0], "seq,t_us,shunt_raw,phase")
        self.assertEqual(csv_lines[1], "0,0,-3,00")
        self.assertEqual(len(csv_lines), 1 + 6)            # only data lines
        self.assertFalse(any(ln.startswith("#") for ln in csv_lines))
        self.assertEqual(side["reader_id"], "v0.1 2026-11-03")
        self.assertEqual(side["config"], "trace")
        self.assertEqual(side["config_register"], "0x199D")
        self.assertEqual(side["r_eff_ohm"], 0.1)
        self.assertIs(side["r_eff_calibrated"], False)
        self.assertEqual(side["vbus_mV_start"], 4990)
        self.assertEqual(side["vbus_mV_end"], 4985)
        self.assertEqual(side["samples"], 6)
        self.assertEqual(side["seq_gaps"], 1)
        self.assertEqual(side["overflows"], 2)
        self.assertEqual(side["dut_commit"], "abc123")
        self.assertEqual(side["note"], "hi")
        self.assertIn("start_time", side)
        self.assertIn("end_time", side)
        self.assertIn("interval_mean_us", side)
        self.assertNotIn("dtim_period", side)               # unset flags omitted
        self.assertIn("samples", summary)

    def test_start_while_running_reports_reader_error(self):
        class Busy(FakeReader):
            def send(self, cmd):
                self.sent.append(cmd)
                if cmd == "id":
                    self.buf.append(self.id_line)
                elif cmd == "vbus":
                    self.buf.append(self.vbus.pop(0))
                elif cmd.startswith("start"):
                    self.buf.append("# err busy")

        port = Busy("# id x y", ["# vbus_mV 4990"], [], "")
        with tempfile.TemporaryDirectory() as d:
            args = record.parse_args(["--port", "C", "--app", "a", "--run", "1", "--out", d])
            with self.assertRaisesRegex(RuntimeError, "busy"):
                record.run(port, args)

    def test_sidecar_written_even_if_final_vbus_fails(self):
        port = FakeReader("# id x y", ["# vbus_mV 4990", "# err busy"], STREAM[1:-1], STREAM[-1])
        with tempfile.TemporaryDirectory() as d:
            args = record.parse_args(["--port", "C", "--app", "a", "--run", "1", "--out", d])
            with self.assertRaises(RuntimeError):
                record.run(port, args, now=datetime(2026, 11, 3, 21, 0, 0))
            side = json.loads(Path(d, "20261103-210000_a_trace_r1.json").read_text())
        self.assertEqual(side["samples"], 6)
        self.assertNotIn("vbus_mV_end", side)

    def test_required_args(self):
        with self.assertRaises(SystemExit):
            record.parse_args(["--port", "COM9", "--config", "trace"])


if __name__ == "__main__":
    unittest.main()

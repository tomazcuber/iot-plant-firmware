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
        self.assertEqual(record.parse_stopped("# stopped n=9 ovf=4"), (9, 4, None, None))
        self.assertEqual(record.parse_stopped("# stopped n=9 ovf=4 i2c=2 t_us=4500"),
                         (9, 4, 2, 4500))
        self.assertIsNone(record.parse_stopped("# ovf 4"))

    def test_i2c_err_line(self):
        self.assertEqual(record.parse_i2c_err("# i2c_err 7"), 7)
        self.assertIsNone(record.parse_i2c_err("# err i2c"))

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

    def test_i2c_errors_are_not_overflows(self):
        s = self.feed(["0,0,1,00", "# i2c_err 5", "# stopped n=9 ovf=0 i2c=7 t_us=4000"])
        self.assertEqual(s.i2c_errors, 7)
        self.assertEqual(s.overflows, 0)

    def test_malformed_lines_are_counted_not_dropped_silently(self):
        s = self.feed(["0,0,1,00", "1,500,1", "garbage", "", "# err something", "2,1000,1,00"])
        self.assertEqual(s.malformed_lines, 2)   # blank and '#' lines are not malformed
        self.assertEqual(s.samples, 2)

    def test_timeweighted_mean_weights_by_hold_time(self):
        # Averaged emits on change: 100 held 9 ms, 200 held 1 ms until stop at 10 ms
        s = self.feed(["0,0,100,00", "1,9000,200,00", "# stopped n=2 ovf=0 i2c=0 t_us=10000"])
        self.assertAlmostEqual(s.shunt_mean, 150.0)
        self.assertAlmostEqual(s.shunt_mean_timeweighted, 110.0)

    def test_timeweighted_mean_without_stop_time_drops_the_last_value(self):
        s = self.feed(["0,0,100,00", "1,9000,200,00"])
        self.assertAlmostEqual(s.shunt_mean_timeweighted, 100.0)

    def test_timeweighted_mean_of_empty_stream(self):
        self.assertEqual(record.StreamStats().shunt_mean_timeweighted, 0.0)

    def test_summary_text_mentions_everything(self):
        text = record.format_summary(self.feed(STREAM))
        for needle in ["samples", "gaps", "overflow", "i2c errors", "malformed", "mean interval",
                       "532", "time-weighted"]:
            self.assertIn(needle, text)

    def test_summary_reports_current_when_converting(self):
        conv = {"r_eff_ohm": 0.1, "offset_lsb": -2.0, "offset_source": "zero-run"}
        text = record.format_summary(self.feed(["0,0,48,00", "1,500,48,00"]), conv)
        self.assertIn("5.000 mA", text)
        self.assertIn("zero-run", text)


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
    def test_committed_file_is_the_2026_11_05_calibration(self):
        cal = record.load_calibration()
        self.assertEqual(cal["r_eff_ohm"], 0.10101)
        self.assertIs(cal["calibrated"], True)
        self.assertEqual(cal["offset_lsb"], -2.442)
        self.assertEqual(cal["loads_ohm"], [47, 100, 220, 1000])

    def test_r_eff_is_the_least_squares_fit_of_the_recorded_readings(self):
        cal = record.load_calibration()
        v = [r["v_shunt_mV"] for r in cal["readings"]]
        i = [r["meter_mA"] for r in cal["readings"]]
        fit = sum(a * b for a, b in zip(v, i)) / sum(b * b for b in i)
        self.assertAlmostEqual(fit, cal["r_eff_ohm"], places=5)
        for r in cal["readings"]:  # v_shunt is the offset-corrected mean
            self.assertAlmostEqual((r["mean_shunt_raw"] - cal["offset_lsb"]) * 0.01,
                                   r["v_shunt_mV"], places=4)

    def test_current_ma(self):
        # 0.01 mV / 0.1 ohm = 0.1 mA per LSB
        self.assertAlmostEqual(record.shunt_raw_to_ma(-3, 0.1), -0.3)
        self.assertAlmostEqual(record.shunt_raw_to_ma(1000, 0.1), 100.0)

    def test_current_ma_subtracts_the_offset(self):
        # raw -2.5 with offset -2.5 is zero current; 47.5 is 50 LSB above it
        self.assertAlmostEqual(record.shunt_raw_to_ma(-2.5, 0.1, -2.5), 0.0)
        self.assertAlmostEqual(record.shunt_raw_to_ma(47.5, 0.1, -2.5), 5.0)


def write_zero_sidecar(d, stem, start_time, offset, zero=True):
    side = {"start_time": start_time, "zero_offset_lsb": offset}
    if zero:
        side["zero"] = True
    Path(d, stem + ".json").write_text(json.dumps(side))


class SessionZero(unittest.TestCase):
    NOW = datetime(2026, 11, 5, 19, 0, 0)

    def test_picks_latest_zero_before_the_run(self):
        with tempfile.TemporaryDirectory() as d:
            write_zero_sidecar(d, "a", "2026-11-05T09:00:00", -2.1)
            write_zero_sidecar(d, "b", "2026-11-05T18:00:00", -2.4)
            write_zero_sidecar(d, "c", "2026-11-05T19:30:00", -9.9)   # after the run
            self.assertEqual(record.find_session_zero(d, self.NOW), (-2.4, "b"))

    def test_ignores_non_zero_runs_and_other_sessions(self):
        with tempfile.TemporaryDirectory() as d:
            write_zero_sidecar(d, "dut", "2026-11-05T18:00:00", -1.0, zero=False)
            write_zero_sidecar(d, "old", "2026-11-04T10:00:00", -2.3)  # > 12 h earlier
            Path(d, "broken.json").write_text("{not json")
            self.assertIsNone(record.find_session_zero(d, self.NOW))

    def test_resolve_offset_precedence(self):
        cal = {"offset_lsb": -2.442}
        with tempfile.TemporaryDirectory() as d:
            args = record.parse_args(["--port", "C", "--app", "a", "--run", "1"])
            self.assertEqual(record.resolve_offset(args, cal, d, self.NOW),
                             (-2.442, "calibration.json"))
            write_zero_sidecar(d, "z", "2026-11-05T18:50:00", -2.5)
            self.assertEqual(record.resolve_offset(args, cal, d, self.NOW), (-2.5, "z"))
            args = record.parse_args(["--port", "C", "--app", "a", "--run", "1",
                                      "--offset-lsb", "-1"])
            self.assertEqual(record.resolve_offset(args, cal, d, self.NOW), (-1.0, "--offset-lsb"))


class FakeSerial:
    """pyserial stand-in: each read() hands back the next scripted chunk."""

    def __init__(self, chunks):
        self.chunks = list(chunks)

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, n):
        return self.chunks.pop(0) if self.chunks else b""


class BulkSerialPort(unittest.TestCase):
    def make(self, chunks):
        port = record.SerialPort.__new__(record.SerialPort)   # skip opening a real port
        port._ser = FakeSerial(chunks)
        port._lines = record.collections.deque()
        port._partial = b""
        return port

    def test_partial_line_survives_a_read_timeout(self):
        port = self.make([b"0,0,1,00\n1,50", b"", b"0,2,00\n"])
        got = [port.readline() for _ in range(5)]
        self.assertEqual(got, ["0,0,1,00", None, "1,500,2,00", None, None])

    def test_many_lines_in_one_read(self):
        port = self.make([b"0,0,1,00\r\n1,500,1,00\r\n# ovf 1\r\n"])
        got = [port.readline() for _ in range(4)]
        self.assertEqual(got, ["0,0,1,00", "1,500,1,00", "# ovf 1", None])
        self.assertFalse(port.has_partial_line())


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
            result = record.run(port, args, now=datetime(2026, 11, 3, 21, 0, 0))
            summary = result.summary
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
        self.assertEqual(side["r_eff_ohm"], 0.10101)
        self.assertIs(side["r_eff_calibrated"], True)
        self.assertEqual(side["offset_lsb"], -2.442)
        self.assertEqual(side["offset_source"], "calibration.json")
        self.assertEqual(side["i2c_errors"], 0)
        self.assertEqual(side["malformed_lines"], 0)
        self.assertNotIn("zero", side)
        self.assertIn("shunt_mean_raw_timeweighted", side)
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
        self.assertIn("mean current", summary)
        self.assertEqual(result.warnings, [])

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

    def test_failed_final_vbus_keeps_the_summary_and_the_sidecar(self):
        port = FakeReader("# id x y", ["# vbus_mV 4990", "# err timeout"],
                          STREAM[1:-1], STREAM[-1])
        with tempfile.TemporaryDirectory() as d:
            args = record.parse_args(["--port", "C", "--app", "a", "--run", "1", "--out", d])
            result = record.run(port, args, now=datetime(2026, 11, 3, 21, 0, 0))
            side = json.loads(Path(d, "20261103-210000_a_trace_r1.json").read_text())
        self.assertIn("samples", result.summary)
        self.assertEqual(len(result.warnings), 1)
        self.assertIn("timeout", result.warnings[0])
        self.assertEqual(side["samples"], 6)
        self.assertIsNone(side["vbus_mV_end"])
        self.assertIn("timeout", side["vbus_error"])

    def test_zero_run_is_marked_and_used_by_the_next_run(self):
        zero_stream = ["0,0,-2,00", "1,500,-3,00", "2,1000,-2,00", "3,1500,-3,00"]
        zero_stopped = "# stopped n=4 ovf=0 i2c=0 t_us=2000"
        with tempfile.TemporaryDirectory() as d:
            args = record.parse_args(["--port", "C", "--app", "acceptance", "--run", "1",
                                      "--zero", "--out", d])
            self.assertEqual((args.config, args.label), ("trace", "zero"))
            port = FakeReader("# id x y", ["# vbus_mV 5000", "# vbus_mV 5000"],
                              zero_stream, zero_stopped)
            result = record.run(port, args, now=datetime(2026, 11, 5, 18, 0, 0))
            zero_side = json.loads(Path(d, "20261105-180000_acceptance_zero_r1.json").read_text())
            self.assertIn("-2.500 LSB", result.summary)
            self.assertIs(zero_side["zero"], True)
            self.assertEqual(zero_side["zero_offset_lsb"], -2.5)

            args = record.parse_args(["--port", "C", "--app", "a", "--run", "2", "--out", d])
            port = FakeReader("# id x y", ["# vbus_mV 5000", "# vbus_mV 5000"],
                              STREAM[1:-1], STREAM[-1])
            record.run(port, args, now=datetime(2026, 11, 5, 18, 30, 0))
            side = json.loads(Path(d, "20261105-183000_a_trace_r2.json").read_text())
        self.assertEqual(side["offset_lsb"], -2.5)
        self.assertEqual(side["offset_source"], "20261105-180000_acceptance_zero_r1")
        self.assertNotIn("zero", side)

    def test_zero_rejects_averaged(self):
        with self.assertRaises(SystemExit):
            record.parse_args(["--port", "C", "--app", "a", "--run", "1", "--zero",
                               "--config", "averaged"])

    def test_malformed_lines_reach_the_sidecar(self):
        data = ["0,0,1,00", "1,500,1", "2,1000,1,00"]
        port = FakeReader("# id x y", ["# vbus_mV 1", "# vbus_mV 1"], data,
                          "# stopped n=3 ovf=0 i2c=1 t_us=1500")
        with tempfile.TemporaryDirectory() as d:
            args = record.parse_args(["--port", "C", "--app", "a", "--run", "1", "--out", d])
            record.run(port, args, now=datetime(2026, 11, 3, 21, 0, 0))
            side = json.loads(Path(d, "20261103-210000_a_trace_r1.json").read_text())
        self.assertEqual(side["malformed_lines"], 1)
        self.assertEqual(side["i2c_errors"], 1)

    def test_required_args(self):
        with self.assertRaises(SystemExit):
            record.parse_args(["--port", "COM9", "--config", "trace"])


if __name__ == "__main__":
    unittest.main()

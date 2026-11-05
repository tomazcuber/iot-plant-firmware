#!/usr/bin/env python3
"""Host recorder for the INA219 current reader (RD-13..RD-16).

Drives one run over the reader's USB-Serial/JTAG port and writes a CSV plus a
JSON sidecar outside the repo. Run from native Windows Python (pyserial).

    python record.py --port COM7 --config trace --app net-smoke --run 1 --seconds 120
    python record.py --port COM7 --zero --app acceptance --run 1 --seconds 10   # VIN- open
"""
import argparse
import collections
import json
import os
import re
import sys
import time
from datetime import datetime
from pathlib import Path

DEFAULT_OUT_DIR = Path(r"C:\zw\bench-data")
CALIBRATION_PATH = Path(__file__).resolve().parent.parent / "calibration.json"
CSV_HEADER = "seq,t_us,shunt_raw,phase"
CONFIG_REGISTERS = {"trace": "0x199D", "averaged": "0x19FD"}
CONVERSION_US = 532  # SADC 12-bit conversion time
SHUNT_LSB_MV = 0.01  # 10 uV per LSB at every PGA setting
REPLY_TIMEOUT_S = 3.0
RX_BUFFER_BYTES = 1 << 20   # Windows driver RX buffer; the default is only ~4 KiB
SESSION_MAX_AGE_H = 12.0    # a zero run older than this is not "this session's" zero

_DATA_RE = re.compile(r"^(\d+),(\d+),(-?\d+),([01]{2})$")
_OVF_RE = re.compile(r"^# ovf (\d+)$")
_I2C_ERR_RE = re.compile(r"^# i2c_err (\d+)$")
_STOPPED_RE = re.compile(r"^# stopped n=(\d+) ovf=(\d+)(?: i2c=(\d+))?(?: t_us=(\d+))?$")
_VBUS_RE = re.compile(r"^# vbus_mV (\d+)$")

RunResult = collections.namedtuple("RunResult", "summary warnings")


# --- line parsing -----------------------------------------------------------

def parse_data_line(line):
    """Return (seq, t_us, shunt_raw, phase) for a data line, else None."""
    m = _DATA_RE.match(line)
    if not m:
        return None
    return int(m[1]), int(m[2]), int(m[3]), m[4]


def parse_ovf(line):
    m = _OVF_RE.match(line)
    return int(m[1]) if m else None


def parse_i2c_err(line):
    m = _I2C_ERR_RE.match(line)
    return int(m[1]) if m else None


def parse_stopped(line):
    """Return (n, ovf, i2c, t_us) for a stop line, else None. i2c and t_us are
    None when the reader's line doesn't carry them (older firmware)."""
    m = _STOPPED_RE.match(line)
    if not m:
        return None
    return (int(m[1]), int(m[2]),
            int(m[3]) if m[3] is not None else None,
            int(m[4]) if m[4] is not None else None)


def parse_vbus(line):
    m = _VBUS_RE.match(line)
    return int(m[1]) if m else None


# --- stream statistics ------------------------------------------------------

class StreamStats:
    """Accumulates gaps, losses and read-interval statistics line by line."""

    def __init__(self):
        self.samples = 0
        self.seq_gaps = 0          # number of seq discontinuities
        self.seq_missing = 0       # total samples missing across them
        self.overflows = 0         # samples the reader lost before the host (RD-09)
        self.i2c_errors = 0        # failed shunt reads, counted apart from overflows
        self.malformed_lines = 0   # non-"#" lines that are not valid data lines
        self.reader_samples = None  # n= from "# stopped"
        self.reader_run_us = None   # t_us= from "# stopped": run length on the reader
        self.interval_count = 0
        self.intervals_ge_532 = 0
        self.interval_max_us = 0
        self._interval_sum = 0
        self._shunt_sum = 0
        self._last_seq = None
        self._last_t = None
        self._last_v = None
        self._tw_sum = 0           # sum of shunt_raw x hold time (LSB x us)
        self._tw_time = 0          # total hold time (us)

    def add_line(self, line):
        if not line:
            return
        if not line.startswith("#"):
            data = parse_data_line(line)
            if data is None:
                self.malformed_lines += 1
            else:
                self._add_sample(*data)
            return
        ovf = parse_ovf(line)
        if ovf is not None:
            self.overflows = ovf
            return
        i2c = parse_i2c_err(line)
        if i2c is not None:
            self.i2c_errors = i2c
            return
        stopped = parse_stopped(line)
        if stopped is not None:
            self.reader_samples, self.overflows, i2c, self.reader_run_us = stopped
            if i2c is not None:
                self.i2c_errors = i2c
            self._close_last_hold()

    def _close_last_hold(self):
        """The last value held until the reader stopped (RD-06)."""
        if self.reader_run_us is not None and self._last_t is not None:
            hold = self.reader_run_us - self._last_t
            if hold > 0:
                self._tw_sum += self._last_v * hold
                self._tw_time += hold

    def _add_sample(self, seq, t_us, shunt_raw, _phase):
        self.samples += 1
        self._shunt_sum += shunt_raw
        if self._last_t is not None and t_us > self._last_t:
            hold = t_us - self._last_t
            self._tw_sum += self._last_v * hold
            self._tw_time += hold
        if self._last_seq is not None and seq != self._last_seq + 1:
            self.seq_gaps += 1
            self.seq_missing += max(seq - self._last_seq - 1, 0)
        if self._last_t is not None:
            dt = t_us - self._last_t
            self.interval_count += 1
            self._interval_sum += dt
            self.interval_max_us = max(self.interval_max_us, dt)
            if dt >= CONVERSION_US:
                self.intervals_ge_532 += 1
        self._last_seq = seq
        self._last_t = t_us
        self._last_v = shunt_raw

    @property
    def interval_mean_us(self):
        return self._interval_sum / self.interval_count if self.interval_count else 0.0

    @property
    def share_ge_532(self):
        return self.intervals_ge_532 / self.interval_count if self.interval_count else 0.0

    @property
    def shunt_mean(self):
        """Plain mean over the emitted lines. Right for Trace only."""
        return self._shunt_sum / self.samples if self.samples else 0.0

    @property
    def shunt_mean_timeweighted(self):
        """Each value weighted by how long it held: until the next line, and the
        last one until the reader stopped. The right mean for Averaged, which
        emits a line only when the value changes (RD-06)."""
        if self._tw_time:
            return self._tw_sum / self._tw_time
        return self.shunt_mean


def format_summary(s, conv=None):
    lines = [
        f"samples:        {s.samples}",
        f"seq gaps:       {s.seq_gaps} ({s.seq_missing} samples missing)",
        f"overflows:      {s.overflows}",
        f"i2c errors:     {s.i2c_errors}",
        f"malformed lines: {s.malformed_lines}",
        f"mean interval:  {s.interval_mean_us:.1f} us (max {s.interval_max_us} us)",
        f"intervals >= {CONVERSION_US} us: {s.share_ge_532 * 100:.3f} %",
        f"mean shunt_raw: {s.shunt_mean:.3f} LSB (time-weighted {s.shunt_mean_timeweighted:.3f})",
    ]
    if conv is not None:
        mean_ma = shunt_raw_to_ma(s.shunt_mean_timeweighted, conv["r_eff_ohm"], conv["offset_lsb"])
        lines.append(f"mean current:   {mean_ma:.3f} mA (offset {conv['offset_lsb']:.3f} LSB "
                     f"from {conv['offset_source']}, R_eff {conv['r_eff_ohm']} ohm)")
    return "\n".join(lines)


# --- files, naming, calibration ---------------------------------------------

def run_stem(when, app, label, config, run):
    return f"{when:%Y%m%d-%H%M%S}_{app}_{label or config}_r{run}"


def resolve_out_dir(out, env):
    if out:
        return Path(out)
    if env.get("INA219_DATA_DIR"):
        return Path(env["INA219_DATA_DIR"])
    return DEFAULT_OUT_DIR


def load_calibration(path=CALIBRATION_PATH):
    with open(path, encoding="utf8") as f:
        return json.load(f)


def shunt_raw_to_ma(shunt_raw, r_eff_ohm, offset_lsb=0.0):
    """current_mA = (shunt_raw - offset) x 0.01 mV / R_eff (mV / ohm = mA)."""
    return (shunt_raw - offset_lsb) * SHUNT_LSB_MV / r_eff_ohm


def find_session_zero(out_dir, before, max_age_h=SESSION_MAX_AGE_H):
    """The most recent zero run (sidecar with "zero": true) that started at or
    before `before`, no more than max_age_h earlier. Returns (offset_lsb, stem)
    or None. A zero can't be taken during a DUT run (current flows), so it is
    its own run."""
    best = None
    for path in Path(out_dir).glob("*.json"):
        try:
            side = json.loads(path.read_text(encoding="utf8"))
            if not side.get("zero") or "zero_offset_lsb" not in side:
                continue
            started = datetime.fromisoformat(side["start_time"])
        except (OSError, ValueError, KeyError, TypeError):
            continue
        age_h = (before - started).total_seconds() / 3600
        if 0 <= age_h <= max_age_h and (best is None or started > best[0]):
            best = (started, side["zero_offset_lsb"], path.stem)
    return (best[1], best[2]) if best else None


def resolve_offset(args, cal, out_dir, when):
    """Offset to convert this run with: --offset-lsb, else the latest zero run of
    the session, else the committed calibration.json value. Returns (lsb, source)."""
    if args.offset_lsb is not None:
        return args.offset_lsb, "--offset-lsb"
    zero = find_session_zero(out_dir, when)
    if zero is not None:
        return zero
    return cal.get("offset_lsb", 0.0), "calibration.json"


# --- serial port ------------------------------------------------------------

class SerialPort:
    """Line-oriented wrapper over pyserial. readline() returns None when no
    complete line is available. It reads in bulk and keeps a partial line across
    reads, so a read timeout never discards bytes that have arrived."""

    def __init__(self, name):
        import serial  # imported lazily so the pure logic tests need no pyserial
        self._ser = serial.Serial(name, timeout=0.05)
        try:
            self._ser.set_buffer_size(rx_size=RX_BUFFER_BYTES)
        except (AttributeError, OSError):
            pass  # not Windows, or the driver refuses: keep the default buffer
        self._ser.reset_input_buffer()
        self._lines = collections.deque()
        self._partial = b""

    def send(self, cmd):
        self._ser.write((cmd + "\n").encode("ascii"))

    def readline(self):
        if not self._lines:
            chunk = self._ser.read(self._ser.in_waiting or 1)  # waits <= timeout for 1 byte
            if chunk:
                *done, self._partial = (self._partial + chunk).split(b"\n")
                self._lines.extend(done)
        if not self._lines:
            return None
        return self._lines.popleft().decode("ascii", errors="replace").strip()

    def has_partial_line(self):
        return bool(self._partial.strip())

    def close(self):
        self._ser.close()


# --- run sequence (RD-14) ---------------------------------------------------

def _expect(port, prefix):
    """Send nothing; read lines until one starts with prefix. Returns the line."""
    deadline = time.monotonic() + REPLY_TIMEOUT_S
    while time.monotonic() < deadline:
        line = port.readline()
        if line is not None and (line.startswith(prefix) or line.startswith("# err")):
            return line
    raise TimeoutError(f"no '{prefix}' reply from the reader")


def _command(port, cmd, prefix):
    port.send(cmd)
    line = _expect(port, prefix)
    if line.startswith("# err"):
        raise RuntimeError(f"reader rejected '{cmd}': {line}")
    return line


def _vbus(port):
    return parse_vbus(_command(port, "vbus", "# vbus_mV"))


def run(port, args, now=None, clock=time.monotonic):
    """Execute one run on an open port. Returns RunResult(summary, warnings);
    warnings is non-empty when the run was recorded but something failed (the
    final vbus snapshot, a partial line left in the receive buffer)."""
    start = now or datetime.now()
    out_dir = resolve_out_dir(args.out, os.environ)
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = run_stem(start, args.app, args.label, args.config, args.run)
    cal = load_calibration()
    offset_lsb, offset_source = resolve_offset(args, cal, out_dir, start)
    warnings = []

    reader_id = _command(port, "id", "# id")[len("# id "):]
    vbus_start = _vbus(port)

    stats = StreamStats()
    started = _command(port, f"start {args.config}", "# started")
    config_register = started.split()[-1] if len(started.split()) >= 4 \
        else CONFIG_REGISTERS[args.config]

    deadline = clock() + args.seconds if args.seconds else None
    with open(out_dir / (stem + ".csv"), "w", encoding="ascii", newline="\n") as csv:
        csv.write(CSV_HEADER + "\n")

        def consume(line):
            if line is None:
                return
            if parse_data_line(line) is not None:
                csv.write(line + "\n")
            stats.add_line(line)

        try:
            while deadline is None or clock() < deadline:
                consume(port.readline())
        except KeyboardInterrupt:
            pass

        port.send("stop")
        stop_deadline = clock() + REPLY_TIMEOUT_S
        while stats.reader_samples is None and clock() < stop_deadline:
            consume(port.readline())
        if stats.reader_samples is None:
            warnings.append("no '# stopped' line from the reader: counts may be incomplete")
        has_partial = getattr(port, "has_partial_line", lambda: False)
        if has_partial():
            stats.malformed_lines += 1
            warnings.append("a partial line was left in the receive buffer (counted as malformed)")
        vbus_end, vbus_error = None, None
        try:
            vbus_end = _vbus(port)
        except (RuntimeError, TimeoutError) as exc:
            vbus_error = str(exc)
            warnings.append(f"final vbus failed: {exc}")

    end = datetime.now() if now is None else start
    conv = {"r_eff_ohm": cal["r_eff_ohm"], "offset_lsb": offset_lsb, "offset_source": offset_source}
    sidecar = {
        "reader_id": reader_id,
        "config": args.config,
        "config_register": config_register,
        "start_time": start.isoformat(timespec="seconds"),
        "end_time": end.isoformat(timespec="seconds"),
        "r_eff_ohm": cal["r_eff_ohm"],
        "r_eff_calibrated": cal["calibrated"],
        "offset_lsb": offset_lsb,
        "offset_source": offset_source,
        "vbus_mV_start": vbus_start,
        "vbus_mV_end": vbus_end,
        "samples": stats.samples,
        "reader_samples": stats.reader_samples,
        "seq_gaps": stats.seq_gaps,
        "seq_missing": stats.seq_missing,
        "overflows": stats.overflows,
        "i2c_errors": stats.i2c_errors,
        "malformed_lines": stats.malformed_lines,
        "interval_mean_us": round(stats.interval_mean_us, 3),
        "interval_max_us": stats.interval_max_us,
        "intervals_ge_532_share": round(stats.share_ge_532, 6),
        "shunt_mean_raw": round(stats.shunt_mean, 4),
        "shunt_mean_raw_timeweighted": round(stats.shunt_mean_timeweighted, 4),
    }
    if vbus_error is not None:
        sidecar["vbus_error"] = vbus_error
    if args.zero:
        # The offset measured by this run, for later runs of the session to use.
        sidecar["zero"] = True
        sidecar["zero_offset_lsb"] = round(stats.shunt_mean, 4)
    for key in OPTIONAL_FIELDS:
        value = getattr(args, key)
        if value is not None:
            sidecar[key] = value
    with open(out_dir / (stem + ".json"), "w", encoding="utf8") as f:
        json.dump(sidecar, f, indent=2)
        f.write("\n")
    summary = format_summary(stats, None if args.zero else conv)
    if args.zero:
        summary += f"\nzero offset:    {stats.shunt_mean:.3f} LSB (recorded as this session's zero)"
    return RunResult(summary, warnings)


# --- CLI --------------------------------------------------------------------

OPTIONAL_FIELDS = ["dut_commit", "power_save", "ap", "dtim_period", "rssi",
                   "read_interval_s", "ble_window_s", "note"]


def parse_args(argv):
    p = argparse.ArgumentParser(description="Record one INA219 reader run.")
    p.add_argument("--port", required=True, help="reader COM port, e.g. COM7")
    p.add_argument("--config", choices=sorted(CONFIG_REGISTERS), default="trace")
    p.add_argument("--app", required=True, help="DUT app name (file name part)")
    p.add_argument("--run", required=True, type=int, help="run number")
    p.add_argument("--label", help="file name label (default: config name, or 'zero')")
    p.add_argument("--seconds", type=float, help="stop after N seconds (default: Ctrl-C)")
    p.add_argument("--out", help=f"output dir (default: $INA219_DATA_DIR or {DEFAULT_OUT_DIR})")
    p.add_argument("--zero", action="store_true",
                   help="zero-offset run: VIN- open, forces Trace, marks the sidecar as the "
                        "session's zero (later runs convert with it)")
    p.add_argument("--offset-lsb", dest="offset_lsb", type=float,
                   help="override the offset used to convert this run (default: latest zero "
                        f"run within {SESSION_MAX_AGE_H:g} h in the output dir, else calibration.json)")
    p.add_argument("--dut-commit", dest="dut_commit")
    p.add_argument("--power-save", dest="power_save")
    p.add_argument("--ap")
    p.add_argument("--dtim-period", dest="dtim_period", type=int)
    p.add_argument("--rssi", type=int)
    p.add_argument("--read-interval-s", dest="read_interval_s", type=float)
    p.add_argument("--ble-window-s", dest="ble_window_s", type=float)
    p.add_argument("--note")
    args = p.parse_args(argv)
    if args.zero:
        if args.config != "trace" and "--config" in argv:
            p.error("--zero measures the offset in Trace; drop --config or use trace")
        args.config = "trace"
        args.label = args.label or "zero"
    return args


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    port = SerialPort(args.port)
    try:
        result = run(port, args)
    finally:
        port.close()
    print(result.summary)
    for w in result.warnings:
        print(f"WARNING: {w}", file=sys.stderr)
    if result.warnings:
        sys.exit(1)


if __name__ == "__main__":
    main()

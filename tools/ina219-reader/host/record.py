#!/usr/bin/env python3
"""Host recorder for the INA219 current reader (RD-13..RD-16).

Drives one run over the reader's USB-Serial/JTAG port and writes a CSV plus a
JSON sidecar outside the repo. Run from native Windows Python (pyserial).

    python record.py --port COM7 --config trace --app net-smoke --run 1 --seconds 120
"""
import argparse
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

_DATA_RE = re.compile(r"^(\d+),(\d+),(-?\d+),([01]{2})$")
_OVF_RE = re.compile(r"^# ovf (\d+)$")
_STOPPED_RE = re.compile(r"^# stopped n=(\d+) ovf=(\d+)$")
_VBUS_RE = re.compile(r"^# vbus_mV (\d+)$")


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


def parse_stopped(line):
    m = _STOPPED_RE.match(line)
    return (int(m[1]), int(m[2])) if m else None


def parse_vbus(line):
    m = _VBUS_RE.match(line)
    return int(m[1]) if m else None


# --- stream statistics ------------------------------------------------------

class StreamStats:
    """Accumulates gaps, overflows and read-interval statistics line by line."""

    def __init__(self):
        self.samples = 0
        self.seq_gaps = 0          # number of seq discontinuities
        self.seq_missing = 0       # total samples missing across them
        self.overflows = 0         # last overflow total reported by the reader
        self.reader_samples = None  # n= from "# stopped"
        self.interval_count = 0
        self.intervals_ge_532 = 0
        self.interval_max_us = 0
        self._interval_sum = 0
        self._shunt_sum = 0
        self._last_seq = None
        self._last_t = None

    def add_line(self, line):
        data = parse_data_line(line)
        if data is not None:
            self._add_sample(*data)
            return
        ovf = parse_ovf(line)
        if ovf is not None:
            self.overflows = ovf
            return
        stopped = parse_stopped(line)
        if stopped is not None:
            self.reader_samples, self.overflows = stopped

    def _add_sample(self, seq, t_us, shunt_raw, _phase):
        self.samples += 1
        self._shunt_sum += shunt_raw
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

    @property
    def interval_mean_us(self):
        return self._interval_sum / self.interval_count if self.interval_count else 0.0

    @property
    def share_ge_532(self):
        return self.intervals_ge_532 / self.interval_count if self.interval_count else 0.0

    @property
    def shunt_mean(self):
        return self._shunt_sum / self.samples if self.samples else 0.0


def format_summary(s):
    return "\n".join([
        f"samples:        {s.samples}",
        f"seq gaps:       {s.seq_gaps} ({s.seq_missing} samples missing)",
        f"overflows:      {s.overflows}",
        f"mean interval:  {s.interval_mean_us:.1f} us (max {s.interval_max_us} us)",
        f"intervals >= {CONVERSION_US} us: {s.share_ge_532 * 100:.3f} %",
        f"mean shunt_raw: {s.shunt_mean:.3f} LSB",
    ])


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


def shunt_raw_to_ma(shunt_raw, r_eff_ohm):
    """current_mA = shunt_raw x 0.01 mV / R_eff (mV / ohm = mA)."""
    return shunt_raw * SHUNT_LSB_MV / r_eff_ohm


# --- serial port ------------------------------------------------------------

class SerialPort:
    """Line-oriented wrapper over pyserial. readline() returns None on timeout."""

    def __init__(self, name):
        import serial  # imported lazily so the pure logic tests need no pyserial
        self._ser = serial.Serial(name, timeout=0.2)
        self._ser.reset_input_buffer()
        self._partial = b""

    def send(self, cmd):
        self._ser.write((cmd + "\n").encode("ascii"))

    def readline(self):
        raw = self._ser.readline()
        if not raw.endswith(b"\n"):
            return None
        return raw.decode("ascii", errors="replace").strip()

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
    """Execute one run on an open port; returns the summary text."""
    start = now or datetime.now()
    out_dir = resolve_out_dir(args.out, os.environ)
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = run_stem(start, args.app, args.label, args.config, args.run)
    cal = load_calibration()

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
        vbus_end, vbus_error = None, None
        try:
            vbus_end = _vbus(port)
        except (RuntimeError, TimeoutError) as exc:
            vbus_error = exc  # still write the sidecar, then re-raise

    end = datetime.now() if now is None else start
    sidecar = {
        "reader_id": reader_id,
        "config": args.config,
        "config_register": config_register,
        "start_time": start.isoformat(timespec="seconds"),
        "end_time": end.isoformat(timespec="seconds"),
        "r_eff_ohm": cal["r_eff_ohm"],
        "r_eff_calibrated": cal["calibrated"],
        "vbus_mV_start": vbus_start,
        "samples": stats.samples,
        "reader_samples": stats.reader_samples,
        "seq_gaps": stats.seq_gaps,
        "seq_missing": stats.seq_missing,
        "overflows": stats.overflows,
        "interval_mean_us": round(stats.interval_mean_us, 3),
        "interval_max_us": stats.interval_max_us,
        "intervals_ge_532_share": round(stats.share_ge_532, 6),
    }
    if vbus_end is not None:
        sidecar["vbus_mV_end"] = vbus_end
    for key in OPTIONAL_FIELDS:
        value = getattr(args, key)
        if value is not None:
            sidecar[key] = value
    with open(out_dir / (stem + ".json"), "w", encoding="utf8") as f:
        json.dump(sidecar, f, indent=2)
        f.write("\n")
    if vbus_error is not None:
        raise vbus_error
    return format_summary(stats)


# --- CLI --------------------------------------------------------------------

OPTIONAL_FIELDS = ["dut_commit", "power_save", "ap", "dtim_period", "rssi",
                   "read_interval_s", "ble_window_s", "note"]


def parse_args(argv):
    p = argparse.ArgumentParser(description="Record one INA219 reader run.")
    p.add_argument("--port", required=True, help="reader COM port, e.g. COM7")
    p.add_argument("--config", choices=sorted(CONFIG_REGISTERS), default="trace")
    p.add_argument("--app", required=True, help="DUT app name (file name part)")
    p.add_argument("--run", required=True, type=int, help="run number")
    p.add_argument("--label", help="file name label (default: config name)")
    p.add_argument("--seconds", type=float, help="stop after N seconds (default: Ctrl-C)")
    p.add_argument("--out", help=f"output dir (default: $INA219_DATA_DIR or {DEFAULT_OUT_DIR})")
    p.add_argument("--dut-commit", dest="dut_commit")
    p.add_argument("--power-save", dest="power_save")
    p.add_argument("--ap")
    p.add_argument("--dtim-period", dest="dtim_period", type=int)
    p.add_argument("--rssi", type=int)
    p.add_argument("--read-interval-s", dest="read_interval_s", type=float)
    p.add_argument("--ble-window-s", dest="ble_window_s", type=float)
    p.add_argument("--note")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])
    port = SerialPort(args.port)
    try:
        print(run(port, args))
    finally:
        port.close()


if __name__ == "__main__":
    main()

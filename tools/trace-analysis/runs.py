"""Load a recorded Run: its sidecar and its samples, converted to mA.

Parsing and conversion come from the recorder (tools/ina219-reader/host/record.py).
"""
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ina219-reader" / "host"))
import record  # type: ignore[import-not-found]  # noqa: E402

V_SUPPLY = 5.0  # main spec, Measurement Procedure: V = 5 V

_STEM_RE = re.compile(r"^\d{8}-\d{6}_(?P<app>[^_]+)_.+_r\d+$")

Sample = tuple[int, float, str, int]  # (t_us, current_mA, phase code, seq samples missing before)


class RunSkipped(Exception):
    """This file cannot be analysed as a Run; the message says why."""


@dataclass(frozen=True)
class Run:
    stem: str
    csv_path: Path
    app: str
    interval_s: float
    r_eff_ohm: float
    offset_lsb: float
    offset_source: str
    r_eff_calibrated: bool

    @property
    def warnings(self) -> list[str]:
        out = []
        if self.offset_source == "calibration.json":
            out.append("no zero run in this session: converted with the calibration.json offset")
        if not self.r_eff_calibrated:
            out.append("R_eff is not calibrated")
        committed = record.load_calibration()["r_eff_ohm"]
        if self.r_eff_ohm != committed:
            out.append(f"R_eff {self.r_eff_ohm} differs from calibration.json ({committed})")
        return out


def load_run(csv_path: Path) -> Run:
    """Read a Run's sidecar. Raises RunSkipped when the Run can't be analysed."""
    path = Path(csv_path)
    match = _STEM_RE.match(path.stem)
    if match is None:
        raise RunSkipped("file name is not a recorder Run name")
    sidecar_path = path.with_suffix(".json")
    try:
        side = json.loads(sidecar_path.read_text(encoding="utf8"))
    except FileNotFoundError:
        raise RunSkipped("no sidecar (interrupted recording?)") from None
    except (OSError, ValueError) as exc:
        raise RunSkipped(f"unreadable sidecar: {exc}") from None
    if side.get("zero"):
        raise RunSkipped("zero run")
    if side.get("config") != "trace":
        raise RunSkipped(f"Reader configuration is {side.get('config')!r}, not trace")
    if side.get("read_interval_s") is None:
        raise RunSkipped("sidecar has no read_interval_s")
    try:
        return Run(stem=path.stem, csv_path=path, app=match["app"],
                   interval_s=float(side["read_interval_s"]),
                   r_eff_ohm=float(side["r_eff_ohm"]), offset_lsb=float(side["offset_lsb"]),
                   offset_source=str(side.get("offset_source", "")),
                   r_eff_calibrated=bool(side.get("r_eff_calibrated", False)))
    except (KeyError, TypeError, ValueError) as exc:
        raise RunSkipped(f"incomplete sidecar: {exc}") from None


class SampleReader:
    """Streams a Run's samples; a long Run is millions of lines."""

    def __init__(self, run: Run) -> None:
        self.run = run
        self.malformed = 0
        self.seq_missing = 0

    def __iter__(self) -> Iterator[Sample]:
        r_eff, offset = self.run.r_eff_ohm, self.run.offset_lsb
        last_seq, last_t = None, None
        with open(self.run.csv_path, encoding="ascii", errors="replace") as f:
            if f.readline().strip() != record.CSV_HEADER:
                raise RunSkipped("CSV header is not the recorder's")
            for line in f:
                data = record.parse_data_line(line.strip())
                if data is None:
                    self.malformed += 1
                    continue
                seq, t_us, raw, code = data
                if last_t is not None and t_us <= last_t:
                    raise RunSkipped(f"t_us does not increase at seq {seq}")
                missing = max(seq - last_seq - 1, 0) if last_seq is not None else 0
                self.seq_missing += missing
                last_seq, last_t = seq, t_us
                yield t_us, record.shunt_raw_to_ma(raw, r_eff, offset), code, missing

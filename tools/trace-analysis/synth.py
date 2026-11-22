"""Synthetic Runs for the tests. These are not measurements: every sidecar says so."""
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

SAMPLE_US = 500
R_EFF_OHM = 0.1      # 1 LSB = 0.1 mA, so test currents map to whole LSBs
OFFSET_LSB = -2.0
SYNTHETIC_NOTE = "SYNTHETIC test data, not a measurement"

Block = tuple[str, float, float]  # (phase code, duration in ms, current in mA)


@dataclass(frozen=True)
class CycleShape:
    boot_ms: float = 300
    boot_ma: float = 40.0
    sensor_ms: float = 200
    sensor_ma: float = 30.0
    wifi_ms: float = 1500
    wifi_ma: float = 120.0
    teardown_ms: float = 100
    teardown_ma: float = 90.0
    residual_ms: float = 5
    residual_ma: float = 25.0
    sleep_s: float = 60.0
    sleep_ma: float = 0.5

    def active_blocks(self) -> list[Block]:
        return [
            ("00", self.boot_ms, self.boot_ma),
            ("01", self.sensor_ms, self.sensor_ma),
            ("10", self.wifi_ms, self.wifi_ma),
            ("11", self.teardown_ms, self.teardown_ma),
            ("00", self.residual_ms, self.residual_ma),
        ]

    def blocks(self) -> list[Block]:
        return self.active_blocks() + [("00", self.sleep_s * 1000, self.sleep_ma)]


def raw_for(current_ma: float) -> int:
    return round(current_ma * R_EFF_OHM / 0.01 + OFFSET_LSB)


def sidecar(interval_s: Optional[float], **overrides: object) -> dict[str, object]:
    side: dict[str, object] = {
        "config": "trace",
        "r_eff_ohm": R_EFF_OHM,
        "r_eff_calibrated": True,
        "offset_lsb": OFFSET_LSB,
        "offset_source": "synthetic-zero",
        "synthetic": True,
        "note": SYNTHETIC_NOTE,
    }
    if interval_s is not None:
        side["read_interval_s"] = interval_s
    side.update(overrides)
    return side


def write_run(directory: Path, stem: str, blocks: Iterable[Block],
              side: Optional[dict[str, object]]) -> Path:
    """Write a Run's CSV (and sidecar unless side is None); returns the CSV path."""
    path = Path(directory) / f"{stem}.csv"
    blocks = list(blocks)
    seq = 1
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("seq,t_us,shunt_raw,phase\n")
        # A sample describes the time before it (ADR-0007), so the first one only opens the
        # trace and each later one covers exactly one SAMPLE_US of its block.
        f.write(f"0,0,{raw_for(blocks[0][2])},{blocks[0][0]}\n")
        for code, ms, ma in blocks:
            raw = raw_for(ma)
            for _ in range(round(ms * 1000 / SAMPLE_US)):
                f.write(f"{seq},{seq * SAMPLE_US},{raw},{code}\n")
                seq += 1
    if side is not None:
        path.with_suffix(".json").write_text(json.dumps(side), encoding="utf8")
    return path


def main_blocks(shapes: Iterable[CycleShape], lead_sleep_ms: float = 1000) -> list[Block]:
    """Lead-in sleep, each Cycle, then one more wake so the last Cycle has a closing rise."""
    shapes = list(shapes)
    blocks: list[Block] = [("00", lead_sleep_ms, shapes[0].sleep_ma)]
    for shape in shapes:
        blocks += shape.blocks()
    blocks += shapes[-1].active_blocks()
    blocks.append(("00", 1000, shapes[-1].sleep_ma))
    return blocks


def write_main_run(directory: Path, shapes: Iterable[CycleShape], run: int = 1,
                   interval_s: float = 60) -> Path:
    stem = f"20261121-1200{run:02d}_main_trace_r{run}"
    return write_run(directory, stem, main_blocks(shapes), sidecar(interval_s))

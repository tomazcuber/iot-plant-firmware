"""Cut a `main` Current trace into Cycles and measure each phase (main spec Steps 4-6).

A Cycle runs from one rise out of the sleep floor to the next. Every sample holds
its value until the next sample's t_us, so the phase energies sum to E_cycle exactly.
"""
from dataclasses import dataclass, field
from typing import Iterable, Optional

from runs import V_SUPPLY, Run, Sample, SampleReader

PHASES = ("boot", "sensor", "wifi", "teardown", "residual", "sleep")
ACTIVE_PHASES = PHASES[:-1]
EXPECTED_SEQUENCE = ("00", "01", "10", "11", "00")
_MARKED_PHASES = {"01": "sensor", "10": "wifi", "11": "teardown"}


@dataclass(frozen=True)
class Settings:
    rise_threshold_ma: float = 10.0  # above the sleep floor, below any awake current
    min_sleep_s: float = 1.0         # a quieter stretch than this is a dip, not Sleep
    min_burst_ms: float = 10.0       # a shorter unmarked burst is a spike on the floor
    max_gap_ms: float = 5.0          # a longer hole in the samples excludes the Cycle
    sleep_tolerance: float = 0.10    # allowed t_sleep deviation from read_interval_s


@dataclass
class _Segment:
    code: str
    high: bool
    start_us: int
    end_us: int
    charge_ma_us: float = 0.0
    seq_missing: int = 0
    max_gap_us: int = 0

    @property
    def awake(self) -> bool:
        return self.high or self.code != "00"


@dataclass
class Cycle:
    run: str
    index: int
    start_us: int
    end_us: int
    sequence: tuple[str, ...]
    t_sleep_s: float
    seq_missing: int
    max_gap_us: int
    charge_ma_s: float
    reason: Optional[str] = None
    durations: dict[str, float] = field(default_factory=dict)  # s; empty if sequence is wrong
    charges: dict[str, float] = field(default_factory=dict)    # mA s

    def duration_s(self, phase: str) -> float:
        return self.durations[phase]

    def current_ma(self, phase: str) -> Optional[float]:
        """Mean current of a phase; None for a phase of zero length."""
        t = self.durations[phase]
        return self.charges[phase] / t if t > 0 else None

    @property
    def e_cycle_mj(self) -> float:
        return self.charge_ma_s * V_SUPPLY


@dataclass
class MainRunResult:
    run: Run
    cycles: list[Cycle]
    malformed: int
    seq_missing: int

    @property
    def included(self) -> list[Cycle]:
        return [c for c in self.cycles if c.reason is None]


def analyze_main_run(run: Run, settings: Settings = Settings()) -> MainRunResult:
    reader = SampleReader(run)
    segments = _segments(reader, settings.rise_threshold_ma)
    stretches = _awake_stretches(segments, settings)
    cycles = _build_cycles(run.stem, segments, stretches)
    _mark_exclusions(cycles, run.interval_s, settings)
    return MainRunResult(run, cycles, reader.malformed, reader.seq_missing)


def _segments(samples: Iterable[Sample], threshold_ma: float) -> list[_Segment]:
    """Run-length encode the trace by (phase code, above threshold)."""
    segments: list[_Segment] = []
    seg: Optional[_Segment] = None
    prev: Optional[Sample] = None
    for sample in samples:
        if prev is not None:
            t0, ma, code, _ = prev
            t1, _, _, missing = sample
            high = ma > threshold_ma
            if seg is None or seg.code != code or seg.high != high:
                seg = _Segment(code, high, t0, t1)
                segments.append(seg)
            hold = t1 - t0
            seg.end_us = t1
            seg.charge_ma_us += ma * hold
            seg.seq_missing += missing
            seg.max_gap_us = max(seg.max_gap_us, hold)
        prev = sample
    return segments


def _merge_close(spans: list[tuple[int, int]], segments: list[_Segment],
                 gap_us: float) -> list[tuple[int, int]]:
    merged: list[tuple[int, int]] = []
    for first, last in spans:
        if merged and segments[first].start_us - segments[merged[-1][1]].end_us < gap_us:
            merged[-1] = (merged[-1][0], last)
        else:
            merged.append((first, last))
    return merged


def _awake_stretches(segments: list[_Segment], s: Settings) -> list[tuple[int, int]]:
    """Index spans (first, last) of the awake parts of the trace.

    Bursts first, so that isolated spikes on the sleep floor can be dropped; then
    bursts closer than min_sleep_s join, because a short dip is not Sleep.
    """
    burst_us = s.min_burst_ms * 1000
    awake = [(i, i) for i, seg in enumerate(segments) if seg.awake]
    bursts = _merge_close(awake, segments, burst_us)
    real = [(a, b) for a, b in bursts
            if segments[b].end_us - segments[a].start_us >= burst_us
            or any(seg.code != "00" for seg in segments[a:b + 1])]
    return _merge_close(real, segments, s.min_sleep_s * 1e6)


def _build_cycles(stem: str, segments: list[_Segment],
                  stretches: list[tuple[int, int]]) -> list[Cycle]:
    cycles = []
    for k, (first, last) in enumerate(stretches):
        closing = stretches[k + 1][0] if k + 1 < len(stretches) else None
        stop = closing if closing is not None else len(segments)
        cycle = _measure_cycle(stem, k, segments[first:last + 1], segments[last + 1:stop])
        if closing is None or first == 0:  # no closing rise, or the rise wasn't recorded
            cycle.reason = "incomplete"
        cycles.append(cycle)
    return cycles


def _measure_cycle(stem: str, index: int, awake: list[_Segment],
                   asleep: list[_Segment]) -> Cycle:
    every = awake + asleep
    sequence: list[str] = []
    for seg in every:
        if not sequence or sequence[-1] != seg.code:
            sequence.append(seg.code)
    t_sleep_us = sum(seg.end_us - seg.start_us for seg in asleep)
    sleep_charge = sum(seg.charge_ma_us for seg in asleep)
    cycle = Cycle(run=stem, index=index, start_us=every[0].start_us, end_us=every[-1].end_us,
                  sequence=tuple(sequence), t_sleep_s=t_sleep_us / 1e6,
                  seq_missing=sum(seg.seq_missing for seg in every),
                  max_gap_us=max(seg.max_gap_us for seg in every),
                  charge_ma_s=sum(seg.charge_ma_us for seg in every) / 1e6)
    if cycle.sequence == EXPECTED_SEQUENCE:
        us = dict.fromkeys(PHASES, 0)
        charge = dict.fromkeys(PHASES, 0.0)
        seen_marker = False
        for seg in awake:
            if seg.code != "00":
                seen_marker = True
                phase = _MARKED_PHASES[seg.code]
            else:
                phase = "residual" if seen_marker else "boot"
            us[phase] += seg.end_us - seg.start_us
            charge[phase] += seg.charge_ma_us
        us["sleep"] = t_sleep_us
        charge["sleep"] = sleep_charge
        cycle.durations = {p: us[p] / 1e6 for p in PHASES}
        cycle.charges = {p: charge[p] / 1e6 for p in PHASES}
    return cycle


def _mark_exclusions(cycles: list[Cycle], interval_s: float, s: Settings) -> None:
    """Give every complete Cycle that can't be averaged its reason.

    A sleep far from read_interval_s was cut short by a soil-alert wake (or a wake
    went unseen), and the Cycle after it is that alert's: both stay out of the
    timer-Cycle statistics (ADR-0009).
    """
    max_gap_us = s.max_gap_ms * 1000
    previous_off = False
    complete = [c for c in cycles if c.reason is None]
    for position, cycle in enumerate(complete):
        sleep_off = abs(cycle.t_sleep_s / interval_s - 1) > s.sleep_tolerance
        if position == 0:
            cycle.reason = "first_cycle"
        elif cycle.sequence != EXPECTED_SEQUENCE:
            cycle.reason = "unexpected_sequence"
        elif cycle.max_gap_us > max_gap_us:
            cycle.reason = "sample_gap"
        elif sleep_off:
            cycle.reason = "sleep_off_interval"
        elif previous_off:
            cycle.reason = "after_interrupted_sleep"
        previous_off = sleep_off

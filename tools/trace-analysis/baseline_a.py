"""Comparison baseline A: mean current over whole publication intervals.

Baseline A drives no Phase markers, so the trace is cut into windows of one
publication interval from its first sample. The first window is discarded, like
the first Cycle of a `main` Run, and so is a partial window at the end.
"""
from collections import defaultdict
from dataclasses import dataclass
from typing import Optional

from cycles import AnalysisThresholds
from runs import V_SUPPLY, Run, SampleReader


@dataclass
class BaselineARunResult:
    run: Run
    interval_means_ma: list[float]   # one per kept interval
    excluded_intervals: list[int]    # window indices lost to a hole in the samples
    charge_ma_us: float
    time_us: int
    malformed: int
    seq_missing: int

    @property
    def n_intervals(self) -> int:
        return len(self.interval_means_ma)

    @property
    def i_a_ma(self) -> Optional[float]:
        return self.charge_ma_us / self.time_us if self.time_us else None

    @property
    def energy_per_publication_mj(self) -> Optional[float]:
        i_a = self.i_a_ma
        return None if i_a is None else i_a * V_SUPPLY * self.run.interval_s


def analyze_baseline_a_run(run: Run, thresholds: AnalysisThresholds = AnalysisThresholds()) -> BaselineARunResult:
    interval_us = round(run.interval_s * 1e6)
    max_gap_us = thresholds.max_gap_ms * 1000
    charge: defaultdict[int, float] = defaultdict(float)
    time: defaultdict[int, int] = defaultdict(int)
    holed: set[int] = set()
    reader = SampleReader(run)
    t_first: Optional[int] = None
    t_prev = 0
    for t_us, ma, _, _ in reader:
        if t_first is None:
            t_first = t_us
        else:  # the value describes the time since the previous read (ADR-0007)
            hole = t_us - t_prev > max_gap_us
            start = t_prev
            while start < t_us:  # a sample's span can straddle a window edge
                window = (start - t_first) // interval_us
                stop = min(t_us, t_first + (window + 1) * interval_us)
                charge[window] += ma * (stop - start)
                time[window] += stop - start
                if hole:
                    holed.add(window)
                start = stop
        t_prev = t_us

    whole = (t_prev - t_first) // interval_us if t_first is not None else 0
    kept = [w for w in range(1, whole) if w not in holed]
    return BaselineARunResult(
        run=run,
        interval_means_ma=[charge[w] / time[w] for w in kept],
        excluded_intervals=[w for w in range(1, whole) if w in holed],
        charge_ma_us=sum(charge[w] for w in kept),
        time_us=sum(time[w] for w in kept),
        malformed=reader.malformed,
        seq_missing=reader.seq_missing,
    )

"""Per-condition statistics: mean, standard deviation and n over Cycles and over Runs."""
from functools import partial
from statistics import mean, stdev
from typing import Callable, Optional

from baseline import BaselineRunResult
from cycles import ACTIVE_PHASES, PHASES, Cycle, MainRunResult
from runs import V_SUPPLY, record

Row = dict[str, object]


def _row(metric: str, unit: str, per_run: list[list[float]]) -> Row:
    """Statistics over all values and over each Run's mean. SD needs two values."""
    per_run = [values for values in per_run if values]
    flat = [v for values in per_run for v in values]
    run_means = [mean(values) for values in per_run]
    return {
        "metric": metric,
        "unit": unit,
        "cycle_mean": mean(flat) if flat else None,
        "cycle_sd": stdev(flat) if len(flat) > 1 else None,
        "cycle_n": len(flat),
        "run_mean": mean(run_means) if run_means else None,
        "run_sd": stdev(run_means) if len(run_means) > 1 else None,
        "run_n": len(run_means),
    }


class MainCondition:
    """All the Runs of `main` at one read interval."""

    def __init__(self, results: list[MainRunResult]) -> None:
        self.results = results
        self.interval_s = results[0].run.interval_s
        self.cycles = [c for r in results for c in r.included]

    def mean_duration_s(self, phase: str) -> float:
        return mean(c.durations[phase] for c in self.cycles)

    def current_ma(self, phase: str) -> float:
        """Charge over time across all Cycles, so current x mean duration = mean charge."""
        time = sum(c.durations[phase] for c in self.cycles)
        return sum(c.charges[phase] for c in self.cycles) / time if time > 0 else 0.0

    @property
    def active_charge_ma_s(self) -> float:
        return sum(self.current_ma(p) * self.mean_duration_s(p) for p in ACTIVE_PHASES)

    @property
    def e_cycle_mj(self) -> float:
        return mean(c.e_cycle_mj for c in self.cycles)

    @property
    def lsb_ma(self) -> float:
        return float(record.shunt_raw_to_ma(1, self.results[0].run.r_eff_ohm))

    def rows(self) -> list[Row]:
        def per_run(value: Callable[[Cycle], Optional[float]]) -> list[list[float]]:
            return [[v for v in map(value, r.included) if v is not None] for r in self.results]

        rows = []
        for phase in PHASES:
            rows.append(_row(f"t_{phase}_s", "s",
                             per_run(partial(Cycle.duration_s, phase=phase))))
            rows.append(_row(f"I_{phase}_mA", "mA",
                             per_run(partial(Cycle.current_ma, phase=phase))))
        rows.append(_row("E_cycle_mJ", "mJ", per_run(lambda c: c.e_cycle_mj)))
        return rows


class BaselineCondition:
    """All the Runs of baseline A at one publication interval."""

    def __init__(self, results: list[BaselineRunResult]) -> None:
        self.results = results
        self.interval_s = results[0].run.interval_s

    @property
    def n_intervals(self) -> int:
        return sum(r.n_intervals for r in self.results)

    @property
    def i_a_ma(self) -> Optional[float]:
        time = sum(r.time_us for r in self.results)
        return sum(r.charge_ma_us for r in self.results) / time if time else None

    def rows(self) -> list[Row]:
        currents = [r.interval_means_ma for r in self.results]
        energies = [[i * V_SUPPLY * self.interval_s for i in values] for values in currents]
        return [_row("I_A_mA", "mA", currents), _row("E_publication_mJ", "mJ", energies)]

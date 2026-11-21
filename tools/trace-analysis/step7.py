"""Step 7 model validation (main spec, Measurement Procedure; ADR-0009).

Fit the active phases at one interval, predict E_cycle at the other with the
static-sleep I_sleep, and compare with the energy integrated from that trace.
Extrapolated figures exist only inside a direction, next to its error, and only
when the check passes in both directions.
"""
from dataclasses import dataclass
from typing import Mapping, Optional

from runs import V_SUPPLY
from summary import BaselineACondition, MainCondition

DEFAULT_TOLERANCE_PCT = 5.0  # the specs set no pass criterion; see the README


@dataclass
class Direction:
    fit_interval_s: float
    target_interval_s: float
    e_predicted_mj: float
    e_measured_mj: float
    error_pct: float
    passed: bool
    i_sleep_ma: float             # static-sleep, used by the prediction
    floor_ma: float               # main's own sleep floor at the target interval
    floor_resolution_ma: float    # one INA219 LSB (static-sleep spec, step 5)
    floor_matches: bool
    floor_effect_pct: float       # energy the floor/I_sleep difference moves, % of measured
    t_sleep_measured_s: float     # used by the prediction
    t_sleep_nominal_s: float
    break_even_s: Optional[float] = None
    break_even_withheld: Optional[str] = None


@dataclass
class Step7Result:
    directions: list[Direction]

    @property
    def passed(self) -> bool:
        return all(d.passed for d in self.directions)


def _direction(fit: MainCondition, target: MainCondition, i_sleep_ma: float,
               tolerance_pct: float) -> Direction:
    t_sleep = target.mean_duration_s("sleep")
    predicted = (fit.active_charge_ma_s + i_sleep_ma * t_sleep) * V_SUPPLY
    measured = target.e_cycle_mj
    error = (predicted - measured) / measured * 100
    floor = target.current_ma("sleep")
    return Direction(
        fit_interval_s=fit.interval_s,
        target_interval_s=target.interval_s,
        e_predicted_mj=predicted,
        e_measured_mj=measured,
        error_pct=error,
        passed=abs(error) <= tolerance_pct,
        i_sleep_ma=i_sleep_ma,
        floor_ma=floor,
        floor_resolution_ma=target.lsb_ma,
        floor_matches=abs(floor - i_sleep_ma) <= target.lsb_ma,
        floor_effect_pct=(floor - i_sleep_ma) * t_sleep * V_SUPPLY / measured * 100,
        t_sleep_measured_s=t_sleep,
        t_sleep_nominal_s=target.interval_s,
    )


def _baseline_a_line(first: Optional[BaselineACondition],
                     second: Optional[BaselineACondition]) -> Optional[tuple[float, float]]:
    """Baseline A's energy per publication as a + b x T (mJ, mJ/s), through its two conditions.

    Each publication costs a fixed amount on top of the associated idle, so I_A itself
    depends on the interval.
    """
    if first is None or second is None or first.i_a_ma is None or second.i_a_ma is None:
        return None
    e_first = first.i_a_ma * V_SUPPLY * first.interval_s
    e_second = second.i_a_ma * V_SUPPLY * second.interval_s
    slope = (e_second - e_first) / (second.interval_s - first.interval_s)
    return e_first - slope * first.interval_s, slope


def _break_even(direction: Direction, fit: MainCondition, passed: bool,
                baselines_a: Mapping[float, BaselineACondition]) -> None:
    """Interval at which main's energy per Cycle equals baseline A's per publication.

    main: (Q_active + I_sleep x (T + d)) x V, where d is how far t_sleep sits from
    the nominal interval at the fit condition. Baseline A: a + b x T.
    """
    if not passed:
        direction.break_even_withheld = "Step 7 did not pass in both directions"
        return
    line = _baseline_a_line(baselines_a.get(direction.fit_interval_s),
                            baselines_a.get(direction.target_interval_s))
    if line is None:
        direction.break_even_withheld = "needs a baseline A result at both intervals"
        return
    fixed_mj, slope = line
    sleep_power = direction.i_sleep_ma * V_SUPPLY
    if slope <= sleep_power:
        direction.break_even_withheld = "baseline A's idle power is not above main's sleep power"
        return
    offset_s = fit.mean_duration_s("sleep") - fit.interval_s
    main_fixed_mj = fit.active_charge_ma_s * V_SUPPLY + sleep_power * offset_s
    break_even = (main_fixed_mj - fixed_mj) / (slope - sleep_power)
    if break_even <= 0:
        direction.break_even_withheld = "main uses less energy at every interval"
        return
    direction.break_even_s = break_even


def validate(first: MainCondition, second: MainCondition, i_sleep_ma: float,
             tolerance_pct: float = DEFAULT_TOLERANCE_PCT,
             baselines_a: Optional[Mapping[float, BaselineACondition]] = None) -> Step7Result:
    pairs = [(first, second), (second, first)]
    directions = [_direction(fit, target, i_sleep_ma, tolerance_pct) for fit, target in pairs]
    result = Step7Result(directions)
    for direction, (fit, _) in zip(directions, pairs):
        _break_even(direction, fit, result.passed, baselines_a or {})
    return result

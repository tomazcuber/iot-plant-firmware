#!/usr/bin/env python3
"""Turn recorded Runs into the thesis energy numbers. Native Windows Python, stdlib only.

    python tools/trace-analysis/analyze.py RUNS... --out DIR --i-sleep-ma 0.02

RUNS are CSV files, directories or glob patterns. Exit status: 0 done, 1 Step 7
failed (files are still written), 2 nothing could be analysed.
"""
import argparse
import csv
import glob
import itertools
import json
import sys
from collections import Counter, defaultdict
from dataclasses import asdict
from datetime import timedelta
from pathlib import Path
from typing import Iterable, Optional, Sequence

import baseline_a
import cycles
import runs
import step7
from summary import BaselineACondition, MainCondition, Row

MAIN_APP = "main"
BASELINE_A_APP = "active-wifi"
_ROW_FIELDS = ["metric", "unit", "cycle_mean", "cycle_sd", "cycle_n",
               "run_mean", "run_sd", "run_n"]


def expand_inputs(inputs: Iterable[str]) -> list[Path]:
    found: set[Path] = set()
    for item in inputs:
        path = Path(item)
        if path.is_dir():
            found.update(path.glob("*.csv"))
        elif path.is_file():
            found.add(path)
        else:  # cmd.exe and PowerShell don't expand wildcards for us
            found.update(Path(p) for p in glob.glob(item) if p.lower().endswith(".csv"))
    return sorted(found)


def data_source(all_runs: Iterable[runs.Run]) -> str:
    return "synthetic" if any(run.synthetic for run in all_runs) else "recorded"


def condition_name(app: str, interval_s: float) -> str:
    return f"{app}_{interval_s:g}s"


def _write_json(path: Path, data: object) -> None:
    with open(path, "w", encoding="utf8", newline="\n") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


def _write_rows(path: Path, rows: list[Row], source: str) -> None:
    with open(path, "w", encoding="utf8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=_ROW_FIELDS + ["data_source"], lineterminator="\n")
        writer.writeheader()
        writer.writerows({**row, "data_source": source} for row in rows)


def _run_info(run: runs.Run, malformed: int, seq_missing: int, kept: int) -> dict[str, object]:
    return {"stem": run.stem, "kept": kept, "malformed_lines": malformed,
            "seq_missing": seq_missing, "r_eff_ohm": run.r_eff_ohm,
            "offset_lsb": run.offset_lsb, "offset_source": run.offset_source,
            "warnings": run.warnings}


def _write_cycles_csv(path: Path, condition: MainCondition) -> None:
    fields = ["run", "index", "start_us", "end_us", "approx_wall_time", "included", "reason",
              "sequence", "seq_missing", "max_gap_us"]
    for phase in cycles.PHASES:
        fields += [f"t_{phase}_s", f"I_{phase}_mA"]
    fields += ["E_cycle_mJ", "data_source"]
    source = data_source(r.run for r in condition.results)
    with open(path, "w", encoding="utf8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for result in condition.results:
            started = result.run.start_time
            for c in result.cycles:
                wall = "" if started is None else (
                    started + timedelta(microseconds=c.start_us)).isoformat(timespec="seconds")
                row: dict[str, object] = {
                    "run": c.run_stem, "index": c.index, "start_us": c.start_us, "end_us": c.end_us,
                    "approx_wall_time": wall,
                    "included": int(c.reason is None), "reason": c.reason or "",
                    "sequence": ">".join(c.sequence), "seq_missing": c.seq_missing,
                    "max_gap_us": c.max_gap_us, "E_cycle_mJ": c.e_cycle_mj,
                    "data_source": source,
                }
                if c.durations:
                    for phase in cycles.PHASES:
                        row[f"t_{phase}_s"] = c.duration_s(phase)
                        row[f"I_{phase}_mA"] = c.current_ma(phase)
                writer.writerow(row)


def _write_main(out: Path, condition: MainCondition, thresholds: cycles.AnalysisThresholds) -> None:
    name = condition_name(MAIN_APP, condition.interval_s)
    rows = condition.rows()
    source = data_source(r.run for r in condition.results)
    excluded = [{"run": c.run_stem, "index": c.index, "start_us": c.start_us, "reason": c.reason,
                 "sequence": ">".join(c.sequence)}
                for r in condition.results for c in r.cycles if c.reason is not None]
    _write_json(out / f"{name}.json", {
        "condition": name, "app": MAIN_APP, "interval_s": condition.interval_s,
        "data_source": source,
        "thresholds": asdict(thresholds),
        "runs": [_run_info(r.run, r.malformed, r.seq_missing, len(r.included))
                 for r in condition.results],
        "metrics": rows, "excluded": excluded,
    })
    _write_rows(out / f"{name}.csv", rows, source)
    _write_cycles_csv(out / f"{name}_cycles.csv", condition)
    counts = Counter(str(e["reason"]) for e in excluded)
    detail = ", ".join(f"{reason} {n}" for reason, n in sorted(counts.items()))
    print(f"{name}: {len(condition.cycles)} Cycles kept from {len(condition.results)} Run(s); "
          f"excluded: {detail or 'none'}")


def _write_baseline_a(out: Path, condition: BaselineACondition) -> None:
    name = condition_name(BASELINE_A_APP, condition.interval_s)
    rows = condition.rows()
    source = data_source(r.run for r in condition.results)
    _write_json(out / f"{name}.json", {
        "condition": name, "app": BASELINE_A_APP, "interval_s": condition.interval_s,
        "data_source": source,
        "i_a_ma": condition.i_a_ma,
        "runs": [_run_info(r.run, r.malformed, r.seq_missing, r.n_intervals)
                 for r in condition.results],
        "metrics": rows,
        "excluded": [{"run": r.run.stem, "interval": w, "reason": "sample_gap"}
                     for r in condition.results for w in r.excluded_intervals],
    })
    _write_rows(out / f"{name}.csv", rows, source)
    print(f"{name}: {condition.n_intervals} intervals kept from {len(condition.results)} Run(s)")


def _step7_report(mains: dict[float, MainCondition],
                  baselines_a: dict[float, BaselineACondition],
                  i_sleep_ma: Optional[float], tolerance_pct: float) -> dict[str, object]:
    usable = [mains[k] for k in sorted(mains) if mains[k].cycles]
    if i_sleep_ma is None:
        return {"status": "not_run", "reason": "pass --i-sleep-ma (static-sleep I_sleep)"}
    if len(usable) < 2:
        return {"status": "not_run", "reason": "needs main Cycles at two read intervals"}
    pairs = []
    for first, second in itertools.combinations(usable, 2):
        result = step7.validate(first, second, i_sleep_ma, tolerance_pct, baselines_a)
        pairs.append({"intervals_s": [first.interval_s, second.interval_s],
                      "passed": result.passed,
                      "directions": [asdict(d) for d in result.directions]})
    return {"status": "passed" if all(p["passed"] for p in pairs) else "failed",
            "tolerance_pct": tolerance_pct, "i_sleep_ma": i_sleep_ma, "pairs": pairs}


def _print_step7(report: dict[str, object]) -> None:
    if report["status"] == "not_run":
        print(f"Step 7 not run: {report['reason']}")
        return
    print(f"Step 7 {str(report['status']).upper()} (tolerance {report['tolerance_pct']:g} %)")
    pairs = report["pairs"]
    assert isinstance(pairs, list)
    for pair in pairs:
        for d in pair["directions"]:
            even = f"break-even {d['break_even_s']:.1f} s" if d["break_even_s"] is not None \
                else f"break-even withheld: {d['break_even_withheld']}"
            floor = "" if d["floor_matches"] else " FLOOR MISMATCH"
            print(f"  fit {d['fit_interval_s']:g} s -> {d['target_interval_s']:g} s: "
                  f"error {d['error_pct']:+.2f} %; floor {d['floor_ma']:.3f} mA vs "
                  f"I_sleep {d['i_sleep_ma']:.3f} mA{floor}; {even}")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Per-phase analysis and Step 7 validation.")
    p.add_argument("runs", nargs="+", help="Run CSV files, directories or glob patterns")
    p.add_argument("--out", required=True, help="directory for the summaries")
    p.add_argument("--i-sleep-ma", type=float, help="I_sleep from static-sleep (multimeter), mA")
    p.add_argument("--step7-tolerance-pct", type=float, default=step7.DEFAULT_TOLERANCE_PCT,
                   help="largest Step 7 error that passes, percent (default %(default)s)")
    p.add_argument("--rise-threshold-ma", type=float,
                   default=cycles.AnalysisThresholds.rise_threshold_ma,
                   help="current separating the sleep floor from awake (default %(default)s)")
    args = p.parse_args(argv)
    out = Path(args.out).resolve()
    if any(Path(item).is_dir() and Path(item).resolve() == out for item in args.runs):
        p.error("--out must not be a directory holding the Runs")
    return args


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    thresholds = cycles.AnalysisThresholds(rise_threshold_ma=args.rise_threshold_ma)
    main_results: defaultdict[float, list[cycles.MainRunResult]] = defaultdict(list)
    baseline_a_results: defaultdict[float, list[baseline_a.BaselineARunResult]] = defaultdict(list)
    skipped: dict[str, str] = {}

    for path in expand_inputs(args.runs):
        try:
            run = runs.load_run(path)
            if run.app == MAIN_APP:
                main_results[run.interval_s].append(cycles.analyze_main_run(run, thresholds))
            elif run.app == BASELINE_A_APP:
                baseline_a_results[run.interval_s].append(
                    baseline_a.analyze_baseline_a_run(run, thresholds))
            else:
                skipped[path.stem] = f"app {run.app!r} is not {MAIN_APP} or {BASELINE_A_APP}"
        except runs.RunSkipped as exc:
            skipped[path.stem] = str(exc)

    for stem, reason in sorted(skipped.items()):
        print(f"skipped {stem}: {reason}")
    if not main_results and not baseline_a_results:
        print("no Run could be analysed", file=sys.stderr)
        return 2

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    mains = {k: MainCondition(v) for k, v in main_results.items()}
    baselines_a = {k: BaselineACondition(v) for k, v in baseline_a_results.items()}
    for _, main_condition in sorted(mains.items()):
        _write_main(out, main_condition, thresholds)
    for _, baseline_a_condition in sorted(baselines_a.items()):
        _write_baseline_a(out, baseline_a_condition)
    _write_json(out / "report.json", {"skipped": skipped})

    source = data_source([r.run for c in mains.values() for r in c.results]
                         + [r.run for c in baselines_a.values() for r in c.results])
    if source == "synthetic":
        print("SYNTHETIC data in this analysis: these results are not measurements")
    report = _step7_report(mains, baselines_a, args.i_sleep_ma, args.step7_tolerance_pct)
    report["data_source"] = source
    _write_json(out / "step7.json", report)
    _print_step7(report)
    return 1 if report["status"] == "failed" else 0


if __name__ == "__main__":
    sys.exit(main())

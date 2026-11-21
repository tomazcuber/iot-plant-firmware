# Trace analysis

Turns recorded `Run`s into the thesis energy numbers: the per-phase breakdown of `main`'s `Cycle`s, energy per `Cycle`, baseline A's energy per publication, and the Step 7 model validation. Specs: main firmware Measurement Procedure Steps 4–7, active-wifi Bench step 5. Decision: [ADR-0009](../../docs/adr/0009-thesis-scope-cut.md).

Native Windows Python 3.10 or later, standard library only. It imports the parser and the mA conversion from `tools/ina219-reader/host/record.py`.

```powershell
python tools/trace-analysis/analyze.py C:\zw\bench-data\20261122-*_main_* C:\zw\bench-data\20261122-*_active-wifi_* --out C:\zw\thesis-results --i-sleep-ma <I_sleep>
```

`--i-sleep-ma` is the `static-sleep` multimeter value. Pass only the Runs of the campaign: every `main` and `active-wifi` Run given is averaged into its condition. Each Run needs its sidecar, recorded with `--read-interval-s`, in the Trace configuration. Anything else is skipped and listed with its reason.

Exit status: 0 done, 1 Step 7 failed (files are still written), 2 nothing could be analysed.

## Output

| File | Content |
|---|---|
| `<app>_<interval>s.csv` | mean, standard deviation and n over Cycles (or intervals) and over Runs |
| `<app>_<interval>s.json` | the same, plus the Runs used, their warnings, the settings and every excluded Cycle |
| `main_<interval>s_cycles.csv` | every Cycle with its status, for checking `t_sensor`/`t_wifi` against the logger's `status` rows |
| `step7.json` | prediction, error, sleep floor and break-even, in both directions |
| `report.json` | files skipped and why |

Times are in s, currents in mA, energies in mJ, with V = 5 V.

## How a trace is read

- **Conversion.** Each Run uses the `R_eff` and session offset in its sidecar ([ADR-0008](../../docs/adr/0008-offset-corrected-least-squares-calibration.md)). A Run with no session zero, or an `R_eff` that differs from `calibration.json`, carries a warning.
- **Integration.** Each sample holds until the next `t_us`. A phase's current is its charge over its duration, so the phase energies sum to `E_cycle` exactly.
- **Cycle.** From one rise out of the sleep floor to the next. A rise is a crossing of `--rise-threshold-ma` (default 10 mA). Unmarked bursts under 10 ms are spikes on the floor, and quiet stretches under 1 s are dips inside the awake part.
- **Phases.** `boot` runs from the rise to the first `01`. `sensor`, `wifi` and `teardown` are the `01`, `10` and `11` codes. `residual` runs from the end of `11` to the sleep floor. `sleep` runs from there to the next rise.

A Cycle is left out of the statistics, and listed with its reason, when it is:

| Reason | Meaning |
|---|---|
| `first_cycle` | the first complete Cycle of a Run |
| `incomplete` | its rise or its closing rise is not in the recording |
| `unexpected_sequence` | its codes are anything other than `00 → 01 → 10 → 11 → 00` |
| `sample_gap` | a hole of more than 5 ms in the samples |
| `sleep_off_interval` | `t_sleep` is more than 10 % from `read_interval_s`, as when a soil alert cuts it short |
| `after_interrupted_sleep` | the Cycle woken by that early wake |

Baseline A uses whole publication intervals from its first sample. The first interval, a partial last one and any interval with a hole over 5 ms are left out.

## Step 7

For each direction, the active phases (boot through residual) are fitted at one interval and the energy at the other is predicted as `(Q_active + I_sleep × t_sleep) × V`. `I_sleep` comes from `static-sleep`, and `t_sleep` is the mean measured at the target interval, reported next to the nominal one.

- **Pass tolerance.** The specs set none. The default of 5 % is a placeholder until the thesis fixes a criterion (`--step7-tolerance-pct`).
- **Sleep floor.** `main`'s measured floor is reported next to `I_sleep`, with the share of the measured energy the difference moves. It is flagged when they differ by more than one INA219 LSB, the agreement the `static-sleep` procedure asks for.
- **Break-even.** The interval where `main`'s energy per Cycle equals baseline A's per publication appears only inside a direction, next to its error, and is withheld unless both directions pass.

## Tests

```powershell
python -m unittest discover -s tools/trace-analysis
```

The tests build synthetic traces in temporary directories (`synth.py`). Their results are not measurements.

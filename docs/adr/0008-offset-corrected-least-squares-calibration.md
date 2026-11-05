---
status: accepted
---

# The reader converts with a per-session zero offset and a least-squares R_eff

The first bench run (2026-11-04/05) showed the INA219 reports a stable, non-zero shunt reading with no current: about −2.4 LSB (−24 µV, −0.24 mA at 0.1 Ω), spread 0.13 LSB over two days. At small currents this is a large error. Without correcting it, the 1 kΩ calibration point (5 mA) reads 5.9 % low and the 220 Ω point 1.3 % low. ADR-0007's conversion, `shunt_raw × 0.01 mV ÷ R_eff` with R_eff from one 100 Ω point, can't absorb it. The conversion becomes `current_mA = (shunt_raw − offset_lsb) × 0.01 mV ÷ R_eff`:

- **The offset is measured per measurement session.** A 10 s Trace run with VIN− open (`record.py --zero`) at the start of the session, since a zero can't be taken while the DUT draws current. Each run is converted with the most recent zero of its session, so a chip that drifts between days is handled.
- **R_eff is the least-squares fit through the origin across all four calibration loads**, `R_eff = Σ(V·I) ÷ Σ(I²)`, with V the offset-corrected mean shunt voltage and I the meter's current. The first fit is 0.10101 Ω. The single 100 Ω point gave 0.10130 Ω and left the 1 kΩ point 1.1 % low. The fit leaves all four points within 0.8 %.

## Considered options

- **Offset correction per session, least-squares R_eff** (chosen). Removes the offset error that dominates at low current and uses the whole current range for the gain.
- **A fixed offset in `calibration.json` only.** Simple, but the offset is a property of the chip and its supply conditions. A stale constant hides drift. It remains as the fallback when a session has no zero run.
- **Subtract the idle floor of every trace.** The DUT draws current in every state, so a trace has no zero reference.
- **Single-point R_eff (100 Ω).** What the spec first prescribed. It weights one point and lets the 1 kΩ point carry the whole offset error.

## Consequences

- Every measurement session starts with a zero run, and the session's data is only as good as that zero. The recorder falls back to `calibration.json`'s `offset_lsb` (flagged in the sidecar) when none exists, and `--offset-lsb` overrides.
- `calibration.json` records R_eff with its loads, meter readings, ranges, run files and method, and a typical `offset_lsb` with its date and source.
- Acceptance compares reader and meter within the HM-1001's *rated* accuracy for the range used, not a fixed 1 %: the reference cannot support a tighter claim. Least squares reduces scatter, not the meter's systematic gain error, so the thesis's absolute uncertainty remains the meter's rating (about ±1.2–2 % in the ranges used). Comparisons between firmware builds, made with the same instrument and calibration, are unaffected.

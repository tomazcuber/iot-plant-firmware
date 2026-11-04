---
status: accepted
---

# The current reader records raw, gap-free shunt samples and converts on the host

Every `Current draw` term except `I_sleep` comes from the INA219 current trace, so how the reader samples determines how far every current figure in the thesis can be trusted. The INA219 runs in **continuous, shunt-only** mode: each 12-bit conversion integrates the shunt voltage over 532 µs, and conversions run back-to-back, so no interval of the trace goes unmeasured and the integral of current over time (the energy) has no holes. The reader reads the shunt register **faster than conversions complete** (every 500 µs). Every conversion is read at least once, and the few duplicates don't bias the means, because each conversion spans the same duration. The reader sends the **raw signed register value**, and the host converts it to mA with a calibrated shunt resistance `R_eff` kept in `tools/ina219-reader/calibration.json`. The chip's calibration and current registers are not used.

## Considered options

- **Continuous, raw, read faster than conversions, host conversion** (chosen). The trace has no gaps, every conversion is captured, and calibration is a host-side constant that can be applied again to old runs.
- **Triggered single conversions.** Each read requests a conversion, so the trace has a gap of one I2C round trip per sample, and short events in those gaps are lost from the energy.
- **Read at 1 kHz.** This meets the original "≥ 1 kHz" target but discards about half the conversions. Means stay unbiased but noisier, and single-conversion events can be missed.
- **Read on the conversion-ready flag.** Each conversion is read exactly once, but clearing the flag requires the calibration register and a power-register read every sample, which is three I2C transactions for no measurement gain.
- **On-chip mA (calibration and current registers).** The reader would emit mA directly, but the shunt's real value would be frozen into the firmware and the data. Correcting it later would mean reflashing, and runs recorded before the correction couldn't be fixed.

## Consequences

- The CSV holds `shunt_raw`, not mA. Every analysis converts with the `R_eff` recorded in the run's sidecar (or a newer one, stated explicitly).
- The reader's absolute accuracy is bounded by the multimeter it is calibrated against. The thesis states that meter's rated accuracy as the uncertainty of the current values.
- A phase edge is resolved to about one conversion (≈ 0.5 ms), because each value describes the 532 µs before it was read.

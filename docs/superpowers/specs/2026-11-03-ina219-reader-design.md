# Design: INA219 Current Reader

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Scope:** the bench instrument that records the current trace of a `Device under test`: reader firmware (`tools/ina219-reader/`), host recorder, instrument acceptance, and the ADR-0006 DTIM gate that is its first use  
**Date:** 2026-11-03  
**Status:** Implemented; bench-accepted 2026-11-05 (results in #13 and #14)  
**Decisions:** [ADR-0007](../../adr/0007-reader-records-raw-gap-free-samples.md) (raw, gap-free samples, converted on the host), [ADR-0008](../../adr/0008-offset-corrected-least-squares-calibration.md) (per-session zero offset, least-squares R_eff)

> This spec makes concrete the "Reader" bullet of the main firmware spec's Measurement Procedure (*samples the INA219 as fast as the I2C link allows … streams `t_us, current_mA, phase` lines*). Where the two differ, this spec wins: the reader streams the raw shunt register, not mA (RD-07), and reads at 2 kHz (RD-05).

---

## Purpose

Every `Current draw` term except `I_sleep` comes from an INA219 current trace (main firmware spec, Measurement Procedure). Comparison baseline A's idle trace is also the evidence for the ADR-0006 DTIM gate (shared-modules spec, Verification on the device). The instrument that produces those traces doesn't exist yet. This spec defines it, how it is accepted as trustworthy, and the gate procedure that is its first job.

The reader is an **instrument, not a measured firmware**. It is never a `Device`, it never runs on the board whose energy is measured, and none of its configuration enters the `Energy model`.

## Consumers

| Procedure | Reader configuration | Source |
|---|---|---|
| ADR-0006 DTIM gate (baseline A idle, `MIN_MODEM` vs `NONE`) | Trace | this spec, DTIM gate |
| `main` current trace (`I_boot`, `I_sensor`, `I_wifi`, `I_ble`, phase durations) | Trace + phase markers | main firmware spec, Steps 1–7 |
| Baseline A trace | Trace | active-wifi spec, Bench step 2 |
| `static-sleep` 60 s cross-check | Trace | static-sleep spec, step 5 |
| `active-ble` steady state | Averaged | active-ble spec, step 2 |

---

## Bench

### Hardware

- **Reader board:** a second MuseLab nanoESP32-C6. Connected to the PC by **one** USB cable, on its native USB port (the ESP32-C6 USB-Serial/JTAG, which enumerates with Espressif's VID `303A`), not the CH343 port.
- **INA219 breakout module** with its on-board 0.1 Ω shunt (R100) between VIN+ and VIN−. I2C address `0x40` (A0 = A1 = GND).
- **Multimeter:** Hikari HM-1001. DC current on the `VΩmA` input (200 mA max, fused 500 mA): **20m** range for loads below 20 mA, **200m** range up to 200 mA. Never the unfused `10A` input. The **2000µ** range is the one the static-sleep procedure uses for `I_sleep`. Rated accuracy: see the table under Instrument acceptance.
- **Calibration loads:** 1 kΩ (¼ W), 220 Ω (1 W), 100 Ω (1 W), 47 Ω (2 W), all 5 %. The tolerance doesn't matter, because the multimeter measures the actual current.

### Wiring

| From | To | Note |
|---|---|---|
| Reader 5V | INA219 VIN+ | supply into the shunt |
| INA219 VIN− | DUT 5V pin (or a calibration load) | measured path |
| Reader GND | INA219 GND, DUT GND | all grounds common |
| Reader 3V3 | INA219 VCC | logic supply |
| Reader GPIO4 | INA219 SDA | I2C, same pins as the `plant_sensors` shield |
| Reader GPIO5 | INA219 SCL | |
| Reader GPIO6 | DUT phase-marker bit 1 (MSB) | input, pull-down |
| Reader GPIO7 | DUT phase-marker bit 0 (LSB) | input, pull-down |

- **No two 5 V sources together.** Disconnect the reader-5V wire before plugging USB into the DUT (to flash it), and reconnect it only after unplugging. A DUT USB cable during a run also bypasses the shunt (main firmware spec, Bench).
- The supply the DUT sees is the PC's USB 5 V after the reader board's 5V-pin path and the shunt's burden (≈ 10 mV at 100 mA). It is not exactly 5.00 V, so it is measured per run (RD-12).
- **Bench fact, confirmed 2026-11-05:** the reader board's silkscreen breaks out GPIO6 and GPIO7, so the overlay needs no change. The reader's native USB port enumerates as `VID:PID=303A:1001`.

---

## Reader firmware

### Requirements

#### RD-01: Placement and build
The reader lives in `tools/ina219-reader/` as a Zephyr app in the same west workspace, built for `esp32c6_devkitc/esp32c6/hpcore`. It is not a measured build. It sets its own Kconfig freely, doesn't use the `plant_sensors` shield or the `plant-net` snippet, links no `plant_*` module, and is outside SM-05 and ADR-0006.

#### RD-02: Data port
Commands and data use the reader's USB-Serial/JTAG device (`espressif,esp32-usb-serial`). The Zephyr console and logging stay on UART0 (CH343 port), so no log line ever interleaves with data.

#### RD-03: Boot state
The reader boots idle: the INA219 is configured but nothing streams until `start`. On boot it prints the `# id` line (RD-10) once.

#### RD-04: Trace configuration
INA219 configuration register `0x199D`:

| Field | Value | Meaning |
|---|---|---|
| BRNG | 0 | 16 V bus range (the bus is ≈ 5 V) |
| PG | /8 | ±320 mV shunt range ≈ ±3.2 A at 0.1 Ω; WiFi TX peaks never clip |
| BADC | 12-bit | used only by the `vbus` snapshot (RD-12) |
| SADC | 12-bit, 1 sample | 532 µs per conversion, no averaging |
| MODE | shunt, continuous | conversions back-to-back, no time gaps |

The register values are derived from the INA219 datasheet's configuration-register table. Implementation re-checks them against the datasheet.

#### RD-05: Read rate
In Trace, the reader reads the shunt-voltage register (`0x01`) on a fixed period of **500 µs** (2 kHz), faster than the 532 µs conversion. Every conversion is read at least once, and about 6 % are read twice. Duplicates don't bias means: every conversion spans the same 532 µs, so uniform reads weight them equally (ADR-0007).

The implementation may use any timing mechanism (`k_timer` with a sufficient tick rate, a hardware counter, a paced loop) as long as RD-17's timing criteria pass.

#### RD-06: Averaged configuration
INA219 configuration register `0x19FD`: as RD-04 but SADC = 128 samples, about 68.1 ms per result. The reader polls at the same 500 µs and **emits a line only when `shunt_raw` changes**. A new result exactly equal to the previous one is not emitted. That is harmless for a mean **only if the mean is time-weighted**: each emitted value is weighted by how long it held, which is until the next emitted line, and for the last one until the reader's `stop` (the `# stopped` line carries the run length as `t_us`). A plain mean over the emitted lines overweights brief values. It is noted here so nobody mistakes the missing lines for dropped samples.

#### RD-07: Raw value
The reader sends the shunt-voltage register as a **signed** integer (`shunt_raw`, 10 µV per LSB at every PGA setting). It does not program the calibration register and does not read the current or power registers. Conversion to mA happens on the host (RD-15, ADR-0007).

#### RD-08: Timestamp and markers
For each read the reader records:
- `t_us`: a µs timestamp from a hardware counter, taken when the I2C read completes. It resets to 0 at `start`, is 64-bit internally and is printed in decimal.
- `phase`: the two marker inputs (GPIO6 = bit 1, GPIO7 = bit 0), sampled right after the read, printed as `00`…`11`.

The value read is the conversion that *ended* up to 532 µs before `t_us`, so a phase edge is resolved to about one conversion (≈ 0.5 ms). This is within the main spec's tolerance of "one sampling period plus GPIO latency".

#### RD-09: Buffering and overflow
A sampling thread fills a ring buffer and a separate writer drains it to the data port. Samples can be lost in two places, and both increment the **overflow** counter: the buffer is full when the sampler has a sample, or the writer gives up on a USB chunk because the host made no progress for 2 s (about the buffer's capacity, so a shorter host stall costs nothing). A failed I2C read is **not** an overflow: it increments a separate **I2C-error** counter. The reader prints `# ovf <total>` and `# i2c_err <total>` when the respective counter changes (each at most once per second) and both final counts at `stop`. `seq` increments for every sample taken, dropped or not, so every loss shows as a gap on the host, and the host can tell a lost sample (counted) from one that was never taken. A chunk dropped mid-write can leave one partial line in the stream, which the recorder counts as malformed (RD-14).

### Wire protocol (RD-10)

Text, one line per message, `\n`-terminated.

| Host → reader | Reply | When |
|---|---|---|
| `id` | `# id <git-describe> <build-date>` | idle or running |
| `vbus` | `# vbus_mV <n>`; `# err timeout` if no conversion completed in 150 ms; `# err i2c` on a bus failure | idle only; else `# err busy` |
| `start trace` / `start averaged` | `# started <config> <config-reg-hex>` then data | idle only; else `# err busy` |
| `stop` | `# stopped n=<samples> ovf=<total> i2c=<total> t_us=<run length>` | running; idle → `# err idle` |
| anything else | `# err unknown` | — |

Data lines: `seq,t_us,shunt_raw,phase`, for example `1048,524000,-3,00`. `seq` starts at 0 at each `start`. Every non-data line starts with `#`.

#### RD-11: Throughput
At 2 kHz a data line is about 25 B, which is about 50 kB/s. The USB-Serial/JTAG link has to sustain this for a full run. RD-17 check 4 verifies it.

#### RD-12: Supply-voltage snapshot
`vbus` (idle only) switches the INA219 to a single triggered bus conversion, waits for it (up to **150 ms**: a 68.1 ms Averaged conversion may still be in progress right after an Averaged run, plus margin), reads the bus-voltage register (`0x02`, bits 15–3, 4 mV/LSB), restores the idle configuration and replies in mV. A conversion that doesn't complete in time replies `# err timeout`, distinct from an I2C bus failure (`# err i2c`). Trace stays shunt-only, so V never costs current-sample rate.

---

## Host recorder

#### RD-13: Tool
`tools/ina219-reader/host/record.py`, Python 3 + `pyserial`, run from **native Windows** Python (WSL can't see COM ports without `usbipd`).

#### RD-14: Run sequence
`record.py --port COMx --config trace|averaged --app <app> --run <n> [--seconds N] [metadata flags]`:
1. `id`, `vbus` → store the replies.
2. `start <config>` → write data lines to the CSV until `--seconds` elapses or Ctrl-C.
3. `stop`, `vbus` → store the replies, close the files.
4. Print a summary: samples, `seq` gaps, overflows, I2C errors, malformed lines, mean read interval, the share of intervals ≥ 532 µs, the mean `shunt_raw` (plain and time-weighted) and the mean current in mA.

Only `--app` and `--run` are required. The recorder reads the port in bulk and keeps a partial line across reads, so a read timeout never discards received bytes. It enlarges the Windows receive buffer and counts every non-`#` line that is not a valid data line as malformed. If the final `vbus` fails, the files are still written, the summary is still printed, `vbus_mV_end` is `null` with the error in the sidecar, and the exit status is non-zero.

**Zero run.** `record.py ... --zero` measures the INA219's offset: VIN− open, Trace, labelled `zero`, with the sidecar marked `"zero": true` and the mean in `zero_offset_lsb`. A zero can't be taken during a DUT run (current flows), so it is its own run, taken at the start of each measurement session.

#### RD-15: Conversion constant
`current_mA = (shunt_raw − offset_lsb) × 0.01 mV ÷ R_eff` ([ADR-0008](../../adr/0008-offset-corrected-least-squares-calibration.md)).

- **R_eff** comes from `tools/ina219-reader/calibration.json`, which is committed and records R_eff, its date, the meter, the method, and the loads, meter readings, ranges and run files used to derive it. It is derived by **least squares through the origin** across all calibration loads: `R_eff = Σ(V·I) ÷ Σ(I²)`, with V the offset-corrected mean shunt voltage and I the meter current. Before any calibration it holds the nominal 0.1 Ω, marked `"calibrated": false`.
- **offset_lsb** is the INA219's zero offset, measured by a zero run (RD-14) at the start of each measurement session. The recorder converts each run with the most recent zero run of its session: the latest zero in the output directory that started before the run and no more than 12 h earlier. `--offset-lsb` overrides it. With no zero run in the session the recorder falls back to the `offset_lsb` in `calibration.json` (a typical value, with its date and source). The sidecar records the offset used and where it came from.
- The recorder copies the R_eff in force and the offset into each sidecar. Because data is raw, a later recalibration can be applied to old runs.

#### RD-16: Run files
- Location: outside the repo. Default `C:\zw\bench-data\`, overridable by `--out` or `INA219_DATA_DIR`. Traces are thesis data, about 7 M lines/hour, and don't belong in git history or within reach of `git clean`.
- Names: `<YYYYMMDD-HHMMSS>_<app>_<label>_r<run>.csv` plus a `.json` sidecar with the same stem. `<label>` defaults to the config name.
- CSV: a header line `seq,t_us,shunt_raw,phase`, then the data lines verbatim. `#` lines go to the sidecar, not the CSV.
- Sidecar, automatic: reader `id`, config and config register, host start/end time (ISO 8601), R_eff and whether it is calibrated, the offset used and its source, `vbus_mV` start/end (`null` plus `vbus_error` if the final one failed), samples, `seq` gaps, overflows, I2C errors, malformed lines, interval statistics, the plain and time-weighted mean `shunt_raw`; for a zero run also `zero` and `zero_offset_lsb`.
- Sidecar, from flags (all optional): DUT firmware commit, power-save mode, AP name, DTIM period, RSSI, `read_interval_s`, `ble_window_s`, free-text note.

Per-phase analysis (main spec Steps 4–7) is **not** part of the recorder (see Delivery slices).

---

## Instrument acceptance (RD-17)

Bench-human checks, recorded as an issue comment in the #6/#7 style. Two levels:

- **Gate-ready** = checks 1, 4, 6. They need no resistors and unblock the DTIM gate, which compares trace *shape* and *relative* means and doesn't depend on calibration.
- **Measurement-ready** = all seven. This unblocks every thesis measurement campaign.

**Meter rated accuracy (Hikari HM-1001 manual).** ±(a % of reading + b digits), 1 year, 23 °C ± 5 °C, RH < 75 %, valid from 5 % to 100 % of the range:

| Function | Range | Resolution | Accuracy |
|---|---|---|---|
| DC V | 200 mV | 100 µV | ±(0.5 % + 2 d) |
| DC V | 2000 mV / 20 V / 200 V / 1000 V | 1 mV / 10 mV / 100 mV / 1 V | ±(0.5 % + 4 d) |
| DC A | 2000 µA / 20 mA | 1 µA / 10 µA | ±(1.0 % + 2 d) |
| DC A | 200 mA | 100 µA | ±(1.2 % + 5 d) |
| DC A | 10 A (unfused, not used) | 10 mA | ±(2 % + 2 d) |

The mA input is fused 500 mA/250 V, and its burden voltage is 200 mV at full scale.

| # | Check | Pass criterion |
|---|---|---|
| 1 | **Zero offset.** VIN− open (no load), Trace, 10 s (`record.py --zero`) | the offset is measured, recorded and stable across the session's zero runs. *Stability threshold: to be set by the user (observed spread: 0.13 LSB over two days)* |
| 2 | **Calibration.** Each of the four loads (1 kΩ on 20m; 220 Ω, 100 Ω and 47 Ω on 200m), meter in series, Trace 30 s, with a zero run in the same session | R_eff = least squares through the origin over the four offset-corrected (V, I) points, written to `calibration.json` with its readings (RD-15) |
| 3 | **Accuracy.** The same four runs, converted with R_eff and the session offset | each mean agrees with the meter **within the HM-1001's rated accuracy** for the range and reading (table above) |
| 4 | **Timing and integrity.** 100 Ω load (or open), Trace 10 min | 0 `seq` gaps, 0 overflows, mean interval ≤ 500 µs, ≥ 99.9 % of intervals < 532 µs |
| 5 | **Averaged.** 100 Ω load, Averaged 60 s vs Trace 60 s | means agree within 0.1 mA |
| 6 | **Markers.** Jumper GPIO6 and/or GPIO7 to 3V3 | CSV shows `01`, `10`, `11` while jumpered and `00` when released |
| 7 | **Supply voltage.** 100 Ω load, `vbus` vs meter (20 V DC) across VIN−/GND, read at the same time | agree **within the HM-1001's rated accuracy** (±(0.5 % + 4 d) on 20 V, so ±65 mV at 4.93 V) |

**Uncertainty statement.** The reader is calibrated against the HM-1001, so it can't be more accurate in absolute terms than that meter's DC-current rating: about ±1.2 % to ±2 % in the ranges used (table above). The thesis states that rating as the absolute uncertainty of every current value. Least squares over four loads reduces reading and rounding scatter, not a systematic gain error of the meter. Relative comparisons between firmware builds are unaffected. Check 3 tests the *consistency* of the calibration across the range, not absolute truth.

A meter reading 0 on the mA ranges means a blown internal fuse, not a dead reader.

---

## DTIM gate (ADR-0006)

The gate checks that `MIN_MODEM` modem sleep takes effect in comparison baseline A, which builds with `CONFIG_PM=n`. It runs on `tests/bench/net-smoke` (associates, then idles, `CONFIG_PM=n`) and needs a gate-ready reader.

### Builds
- `MIN_MODEM` (the subject): `net-smoke` as configured.
- `NONE` (control, reference only, **never** a baseline A measurement): the same app built into its own directory with `-- -DCONFIG_PLANT_NET_WIFI_PS_NONE=y`.

### Procedure
1. Use the home router, not the phone hotspot, whose DTIM is unknown and may vary. Put the DUT at a fixed position close enough for RSSI better than −60 dBm, and use the same spot for all later campaigns. If the router's admin page shows its DTIM setting, note it.
2. For each run: flash the build, unplug the DUT's USB, connect the reader-5V wire, wait **30 s** after power-up for association to settle, then record **120 s** in Trace.
3. **3 runs per build**, alternating `MIN_MODEM` and `NONE`.

### Criterion
- **Pass:** in every `MIN_MODEM` run the idle trace shows short periodic spikes whose spacing is a consistent multiple *n* of 102.4 ms. *n* is the effective DTIM period, and it must match the router's setting if that is visible. In addition, the `MIN_MODEM` idle mean is below the `NONE` mean in every run pair, by more than the run-to-run spread within each build.
- **Fail:** the `MIN_MODEM` idle trace is flat at the `NONE` level. Stop and reopen ADR-0006. This is not a bench adjustment.

### Record
A comment on the gate issue with one plot per build, the means, *n*, the router's DTIM setting (or "not shown"), RSSI and the run files' names.

---

## Delivery slices

GitHub issues, in the #6/#7 style (agent-verifiable vs bench-human checklists):

1. **#13 Reader firmware + host recorder.** RD-01…RD-16, both configurations, marker inputs, wire protocol. Agent-verifiable: builds, `.config`/devicetree checks, recorder tested against a recorded or simulated stream. Bench-human: acceptance checks 1, 4, 6 (gate-ready).
2. **#14 Measurement-ready acceptance.** Checks 2, 3, 5, 7 and the first committed `calibration.json`. Blocked by #13 and by the resistors arriving.
3. **#15 ADR-0006 DTIM gate.** The procedure above. Blocked by #13 (gate-ready). [PR #12](https://github.com/tomazcuber/iot-plant-firmware/pull/12) already refers to it as tracked separately.

Follow-ups, not in this slice:
- FR-MA-18 phase markers in `apps/main` (DUT side).
- Per-phase analysis script (main spec Steps 4–7: per-phase means and durations, t_boot / I_boot, energy from the trace).

---

## Design decisions

### DD-RD-01: Raw, gap-free samples converted on the host
See [ADR-0007](../../adr/0007-reader-records-raw-gap-free-samples.md).

### DD-RD-02: Read at 2 kHz rather than 1 kHz or on conversion-ready
At 1 kHz about half the conversions are never read. The mean stays unbiased but noisier, and single-conversion events can be missed. Reading on the conversion-ready flag would read each conversion exactly once, but clearing that flag requires programming the calibration register and reading the power register every sample, which triples I2C traffic for no gain. Reading slightly faster than conversions catches every one at the cost of a few duplicates. The main spec's "≥ 1 kHz" target is met.

### DD-RD-03: Configuration chosen by host command, not Kconfig
One reader build serves both configurations. The configuration is echoed into the run's sidecar, so it is recorded with the data rather than implied by which image was flashed. No reflash between procedures.

### DD-RD-04: Text over native USB
At about 50 kB/s the native USB-Serial/JTAG link has ample headroom, so the baud rate doesn't matter. Text lines can be read with any terminal, and `seq` detects loss. A binary framing is only worth it well above 2 kHz.

### DD-RD-05: Supply voltage measured, not assumed
The energy model's V was "5 V" by assumption. The DUT's supply passes through the reader board's 5V path and the shunt. A snapshot at each run's start and end costs nothing in sample rate (Trace stays shunt-only) and lets the thesis state the V it used.

### DD-RD-06: Two acceptance levels
The DTIM gate compares the shape and relative level of two traces from the same instrument, so a shunt that is 1 % off changes neither answer. Splitting acceptance lets the gate run before the calibration loads arrive, while every absolute measurement still waits for full calibration.

---
status: accepted
---

# A wake with no recorded cause but a latched GPIO6 is a soil alert

*2026-11-21, PR #33, issue #26.*

The ESP32-C6 can leave `Sleep` on the GPIO6 (EXT1) wake with its PMU wake-cause register empty. The HAL then returns `ESP_SLEEP_WAKEUP_UNDEFINED`. `main` used to treat every cause other than EXT1 as a timer wake, so it published `wake_reason = "timer"` and the armed 60 s as `t_sleep_ms` for a `Sleep` that had ended early. We now fall back on the EXT1 block's own registers: when the cause is empty and GPIO6 is both selected and latched there, the `Wake reason` is `"soil_moisture_alert"`.

## Evidence (bench, 2026-11-21, firmware `d29a55a`, 60 s interval)

- A diagnostic build logged the raw registers with the probe held at the comparator threshold, where the LM393 output flickers. Logger row 1646 woke after 17.64 s with PMU `status0 = 0x00000000`, `ext1_sel = 0x40` and `ext1_status = 0x40`, and was published as `timer` with `t_sleep_ms = 60000`.
- That was 1 of 4 early wakes in the run. The other 3 recorded the EXT1 bit and were published correctly.
- Rows 1609, 1614, 1666 and 1667 show the same symptom without a register capture. Rows 1666 and 1667 arrived 32.9 s and 4.3 s after the row before them, each claiming a 60 s `Sleep`.
- The mislabel also bypassed the wake-loop rule of [ADR-0009](0009-thesis-scope-cut.md), which starts timer-only `Sleep` after an alert. Rows 1666 and 1667 kept GPIO6 armed while DO was dry and cost two extra `Active phase`s. After row 1646, DO had already returned to normal, so that crossing was never published as an alert.

Why the register is sometimes empty is not established. Clean crossings, with DO moving once and staying there, recorded the EXT1 bit every time.

## Considered options

- **Fall back on the EXT1 selection and latch** (chosen). It uses state the hardware already holds and changes no wire format. Both registers are needed: the latch stays set through a later timer-only `Sleep`, when the selection is empty.
- **Keep the default to `timer`** (rejected). It corrupts the timer `Cycle`s the thesis comparison is built on.
- **Read DO at wake instead** (rejected). DO can already be back on the normal side, as after row 1646.
- **Publish a third `Wake reason` such as `"unknown"`** (rejected). It adds a value to the wire vocabulary of [ADR-0002](0002-wake-reason-wire-string.md) for a wake whose cause the latch already identifies, and it wouldn't start the wake-loop rule.
- **Add hysteresis to the comparator** (future work). A feedback resistor on the LM393 would reduce the flicker that exposes this, but it changes the sensor module and leaves the misclassification in place.

## Verification

A host regression test built from row 1646 failed before the fix and passes with it. On hardware, an instrumented build of the fix published normal alerts and timer wakes correctly (rows 1692-1696), but no empty-cause wake occurred in the 5 minutes logged, so **the fallback itself has not run on hardware**. These wakes appeared roughly every 10 to 20 minutes with the probe at the threshold. The user accepted the fix without that longer test (2026-11-21).

## Consequences

- The latch must be read before `esp_deep_sleep_wakeup_io_reset()`, which clears the EXT1 selection. `main` reads the `Wake reason` before it reads DO.
- A recorded timer cause stays `"timer"` even if GPIO6 latched too. That `Sleep` did run its full length, and the wake stays armed, so it fires on the next `Sleep` if DO is still asserted.
- An empty cause with no latch still reports `"timer"`. No such wake has been observed.
- Rows 1609, 1614, 1646, 1666 and 1667 keep their wrong labels. They are bench rows from before this fix and must not enter the measurement campaign.

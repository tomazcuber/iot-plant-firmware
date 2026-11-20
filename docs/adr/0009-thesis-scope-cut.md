---
status: accepted
---

# Thesis scope cut: a reduced `main` compared against baseline A

*2026-11-12. Later decisions are dated where they appear.*

The thesis text is due around 2026-11-26, and the `main` measurement campaign has to start about a week before that. The core result is the measured energy comparison between `main` and comparison baseline A (`apps/active-wifi`, [ADR-0006](0006-comparison-baseline-same-stack.md)) at read intervals of 60 s and 300 s. Everything the specs describe that doesn't serve that comparison is cut from the thesis build. The cut features stay in the specs as the product design: they are future work, not missing work.

## What is cut

- **`apps/active-sensor` and `apps/active-ble`.** Neither is built. `I_sensor` comes from `main`'s trace; with BLE gone there is no `I_ble` to cross-check.
- **BLE in `main`:** provisioning and streaming (FR-MA-01..05, FR-MA-13..15), built with `CONFIG_BT=n`. The comparison runs `main` with no BLE, the specs' `ble_window_s = 0` case (DD-AW-02). `status` still carries `t_ble_ms`, always 0.
- **The Android client** that would have demonstrated provisioning and streaming.
- **Settings/NVS** and therefore `Provisioning mode`: `main` boots straight into its `Cycle`.
- **The re-provisioning button** (FR-MA-02, [ADR-0004](0004-reprovision-detection.md)).

## What replaces it

- **Compile-time configuration** instead of provisioning, as in baseline A: app Kconfig for the read interval, the device name, the WiFi credentials and the broker, with the credentials in a gitignored `credentials.conf`. Each interval condition (60 s, 300 s) is a committed conf fragment and a pristine rebuild.
- **`cycle_count` in retained RAM**, next to `magic` (FR-MA-06), instead of a Settings key, so a `Cycle` writes no flash. It resets on power loss or reflash, together with the rest of the retained struct; that is accepted. A gap in `cycle_count` within one power-on stretch still shows a lost publication.

## What stays

- **The soil-moisture wake** (user decision, 2026-11-13: "keep it as long as possible"). It is part of the proposal, not optional. Alert `Cycle`s run the same `Active phase` as timer `Cycle`s; the thesis reports a few triggered alert cycles descriptively (energy and phase durations next to a timer cycle). They are outside the `main` vs baseline A comparison, which uses timer cycles only.
- **The soil-alert wake-loop rule** (user decision, 2026-11-17). The LM393 output is a level, not an edge: if the GPIO6 wake is armed while the output still says "dry", the chip wakes again at once. So:
  - after a `soil_moisture_alert` wake, the device sleeps with only the RTC timer armed (remembered in the retained struct);
  - each later timer wake reads the comparator output, and re-arms the GPIO6 wake for the next sleep only if it is back on the normal side;
  - a timer wake with the GPIO6 wake already armed changes nothing.

  Result: one alert per dry spell, and while the soil stays past the threshold the device behaves like the timer-only cycle (at most one wake per interval). Rejected: re-arming on every sleep (a wake loop: the device never sleeps while the soil is dry) and emulating an edge trigger with extra retained state (more state and more logic for the same observable behaviour).
- **Phase markers, now a requirement** (reopened by the user 2026-11-20). The first version of this cut also dropped the proposed FR-MA-18 markers, on the reasoning that energy per cycle can be integrated from the trace without them. That drop had never been put to the user as a decision; once it was, the markers came back: the `Reader` already records the two marker inputs on every sample, and they turn the per-phase breakdown from an estimate into a measurement. Codes: `01` sensors, `10` WiFi, `11` **Teardown**, `00` otherwise. `11` was BLE; with BLE cut it marks the teardown instead: from the instant `t_wifi_ms` stops (just before `status` is encoded) through the `status` publish, the delivery confirmation ([ADR-0010](0010-confirm-delivery-before-sleep.md)) and the MQTT and WiFi disconnect, released to `00` right before `sys_poweroff()`. A `Cycle` whose WiFi or MQTT step failed still marks its teardown. The DUT drives the markers on GPIO18 (bit 1) and GPIO19 (bit 0).
- **Baseline A drives no markers** (user decision, 2026-11-13, confirmed 2026-11-20). Its result is a mean current over whole publication intervals, which needs no segmentation. Markers are measurement instrumentation, not part of the stack under comparison, so this is not a stack difference in the sense of ADR-0006. Their cost in `main` is two GPIOs driving the `Reader`'s pull-downs during the `Active phase`, negligible next to `I_wifi`.

## Reduced energy model

- **The deliverable** is `main`'s energy per `Cycle`, integrated directly from its `Current trace`, against baseline A's energy per publication (`I_A × V × interval`), at 60 s and 300 s.
- **The descriptive breakdown** of `main`'s `Cycle` comes from the markers: `E_cycle = (I_sleep × t_sleep + I_boot × t_boot + I_sensor × t_sensor + I_wifi × t_wifi + I_teardown × t_teardown) × V`. `t_boot` (current rise to the first `01`) and `t_teardown` (the `11` segment) exist only in the trace; `t_sensor` and `t_wifi` are also published in `status`, and the two must agree within about one sample. There is no `I_ble` term.
- **Model validation (main spec, Measurement Procedure Step 7) is required** (user decision, 2026-11-20): fit the per-phase currents and durations at one interval, predict `E_cycle` at the other, and compare with the energy integrated from the trace there. It is what entitles the thesis to claims outside the two measured intervals, such as the interval at which `main` and baseline A break even, or a projected battery life.

## Consequences

- The `main`, `active-sensor` and `active-ble` specs carry a status note pointing here. The `main` spec's FR-MA-16, FR-MA-18 and Measurement Procedure are amended inline for the marker codes and the teardown; the rest of its text is left as the product design.
- The Teardown has no `status` field, because `status` is encoded before it starts. It is measured from the trace only, like `t_boot`.
- If BLE comes back, the marker encoding needs a redesign: two bits no longer fit sensors, WiFi, teardown and BLE.
- Work is tracked on GitHub: the reduced `main` (#22), retained state (#24), the soil wake (#26), the phase markers (#28) and the per-phase analysis with Step 7 (#29).

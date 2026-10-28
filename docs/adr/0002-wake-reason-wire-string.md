---
status: accepted
---

# `Wake reason` wire string names the cause, not the mechanism

The `status.wake_reason` MQTT field carries `"timer"` or `"soil_moisture_alert"` — not `"gpio"`. The string names *why* the cycle ran (a threshold crossing in the soil moisture sensor) rather than *how* the wake was delivered (an LP-domain GPIO interrupt). This survives hardware revisions (the LM393 could be replaced with a digital sensor's interrupt; the pin could move off GPIO6) and extends cleanly if a second event source is added later (e.g. `"light_threshold_alert"` from a BH1750FVI INT pin), without retroactively breaking the dataset that the thesis is built on.

## Considered options

- **`"gpio"`** (rejected). Brief and matches what the firmware literally observes at wake. Loses on extensibility — a second GPIO-driven event source would make the string ambiguous — and lies if the wake mechanism is later re-routed off GPIO.
- **`"soil_moisture_alert"`** (chosen). Describes the cause. Verbose (22 bytes vs 5) but trivial at MQTT wire scale.
- **`"event"`** (rejected). Matches DD-MA-01's analytical vocabulary but pushes the "which event?" question downstream to every consumer.

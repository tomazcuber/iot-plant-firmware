---
status: accepted
---

# `active-sensor` baseline holds the device in a tight read loop with radios off

`I_sensor` is defined as the *time-average* current of the device executing the sensor phase, not the instantaneous current during a single I2C transaction. The `apps/active-sensor` baseline firmware therefore initialises the same I2C bus and ADC as `apps/main`, then calls `sensors_read_all()` in a tight `while (1)` loop with no sleeps and no radios. The multimeter's averaging window absorbs the bursty per-read transactions and reports a steady-state current that, when multiplied by `t_sensor` measured in `apps/main`, yields the per-cycle sensor energy.

## Considered options

- **(α) Tight read loop, radios off** (chosen). Matches the main firmware's sensor-phase current profile by construction — same I2C bus state, same ADC state, same CPU activity ratio.
- **(β) Peripherals enabled, CPU sleeping between idle ticks** (rejected). Misses the CPU-on portion that dominates the actual sensor phase.
- **(γ) No separate baseline; integrate current during one sensor phase of `apps/main` on a current-sensing scope** (rejected). More accurate in principle but requires equipment beyond a multimeter and departs from the "one `Baseline firmware` per `Current draw` term" structure that makes the `Energy model` reproducible.
- **(δ) Drop the `I_sensor × t_sensor` term, fold into `t_active`** (rejected). Loses per-phase breakdown — the thesis's whole point is to attribute energy to phases.

## Consequences

- The baseline current reported by `active-sensor` is only meaningful if its sensor-read routine matches `apps/main`'s sensor-read routine. The two implementations should share a common module (`read_all_sensors()`) once `apps/main`'s sensor phase is built, to keep them in lockstep.
- `apps/active-sensor/prj.conf` already disables radios (`CONFIG_WIFI=n`, `CONFIG_BT=n`, `CONFIG_PM=n`) — this matches the decision and should stay.
- If the read order in `apps/main` changes (e.g. HDC1080 moves earlier in the sequence), `active-sensor` must be rebuilt to match before `I_sensor` is re-measured.

---
status: accepted
---

# Active phase boundary: ship with the narrow bracket, pivot if drift is significant

The PRD's `t_active` brackets (FR-MA-16) start at step 1 (first `k_uptime_get()` inside `main()`) and end at step 16 (after BLE stops), leaving two CPU-on segments outside any `t_*` term: a **boot prologue** (ROM bootloader + Zephyr init, before step 1) and a **sleep epilogue** (steps 17–19: cycle-counter persist, retained-RAM write, `pm_state_force`). The `Energy model` `E_cycle = Σ(I_* × t_*) × V` therefore does not strictly conserve over `Cycle` time. We ship `apps/main` with the brackets as written, treat the unmodelled CPU-on overhead as ~1 % of `E_cycle` (with disclosure), and re-evaluate after measuring the boot prologue empirically on the target hardware.

## Considered options

- **(A) Status quo + caveat.** Keep brackets as written. Disclose unmodelled overhead in the thesis methodology. **Chosen as the first milestone.**
- **(B) Extend `t_active_end` to after step 19; add `t_epilogue_ms` to the `status` payload.** Restates the FR-MA-10 invariant as `t_active = t_sensor + t_wifi + t_ble + t_epilogue`. **Planned pivot if measurements show drift > ~2 % of `E_cycle` in any thesis-relevant configuration.**
- **(C) Five-term energy model.** Add a fifth `Baseline firmware` to measure `I_overhead`. Rejected: doubles the baseline-measurement matrix for marginal accuracy gain.
- **(D) Fold the epilogue into `t_sensor`.** Rejected: breaks the "phase = contiguous time slice" mental model and obscures what `t_sensor` represents.

## Consequences

- Thesis claims must distinguish *differential* energy comparisons (timer vs GPIO `Wake reason`; BLE on vs off; `read_interval_s` sweeps) — which are unaffected by the symmetric overhead — from *absolute* claims (E_cycle in mJ; projected battery lifetime), which are biased low by ~1 % in default configs and up to ~8 % in the long-interval / BLE-off corner.
- One empirical task gates the pivot: toggle a GPIO at the very first line of `main()` and again at step 1; measure boot-prologue duration once on the target hardware. If it exceeds ~150 ms, re-evaluate.
- FR-MA-10's exact-equality invariant `t_active == t_sensor + t_wifi + t_ble` remains as written for milestone A. The known residual (steps 15→16 plus the epilogue's exclusion) is small but non-zero; firmware assertions should use a tolerance, not strict equality.

---
status: accepted
---

# Re-provisioning hold is detected via GPIO9 interrupt + software timer, with a deferred reboot

The 3-second BOOT-button hold (FR-MA-02) is detected by registering a falling-edge interrupt on GPIO9 at the start of every `Active phase` and starting a 3-second `k_timer` from within the ISR. A rising edge before the timer expires cancels it (button released). On timer expiry, the firmware sets a `re_provision_requested` flag; wake-cycle steps check it at safe abort points and defer the actual `settings_delete` + reboot until *after* the current cycle's MQTT publish completes. The press itself can begin before or during the `Active phase` — the firmware only cares that 3 s of continuous hold elapses while the device is awake.

## Considered options

- **(α) Tight poll for 100 ms at active-phase start** (rejected). Cheap, but the user can't see when the active phase begins, so the press window is invisible from outside.
- **(β) Interrupt + software timer + deferred reboot** (chosen). Free in the common no-press case; the reboot is deferred so the current cycle's data is preserved.
- **(γ) Dedicated low-priority polling thread** (rejected). Same UX as β; adds permanent thread overhead and synchronisation complexity for no UX gain.
- **(δ) Single read at wake-entry only** (rejected). Abandons the "hold for 3 s" spec language and requires a visible blink-to-confirm UI.

## Consequences

- **Re-provisioning is only reliable when `t_active ≥ 3 s`.** The 3-second timer cannot survive `Sleep` (GPIO9 is not in the LP domain, so the interrupt is dead during sleep and the timer is gone with the kernel). With `ble_window_s = 0` and a clean network (`t_sensor ≈ 100 ms`, `t_wifi ≈ 1 s`), `t_active ≈ 1.1 s` — the user cannot physically complete a 3-second hold within one active phase. **In practice, re-provisioning requires `ble_window_s ≥ ~2 s`** to give the user enough awake time to complete the hold. This is a stricter constraint than the PRD's "Known limitations" note suggests and should be added to the user-facing provisioning documentation.
- The wake cycle steps gain a single `re_provision_requested` flag check after step 10 (status publish complete) and before step 13 (BLE advertising). If set: tear down WiFi cleanly, skip the BLE window, wipe `Settings`, reboot. About 5 lines of glue in `apps/main`.
- The interrupt + timer pair is energetically free in the common no-press case (interrupt is dormant, timer is uncreated). On press, the active phase extends by up to 3 s — visible in `t_active_ms` for that cycle but distinguishable in post-processing because the cycle ends in a reboot rather than a normal MQTT publish of the *next* cycle.
- The re-provisioning *abort* path means a cycle whose status publish has already happened may be followed immediately by a reboot. The `device/cycle_count` increment (step 18) and retained-RAM write (step 19) must happen *before* the wipe, or post-reboot reasoning about "last cycle before re-provisioning" breaks.

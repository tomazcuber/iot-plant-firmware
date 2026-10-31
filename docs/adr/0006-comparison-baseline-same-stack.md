---
status: accepted
---

# Comparison baseline A uses the same stack as `main`; only the architecture differs

Comparison baseline A (`apps/active-wifi`) is the always-connected pattern reported in the literature: WiFi stays associated, and the device reads, publishes and waits in a loop without sleeping. We build it from exactly the same sensor read, JSON encoder, WiFi/MQTT/SNTP primitives and network settings as `apps/main`. The only differences are in each app's orchestration: when it connects, how often it syncs time, and whether it sleeps. The measured energy gap between baseline A and `main` is then attributable to the architecture (deep-sleep cycles vs always connected), not to configuration differences between two hand-written stacks.

The WiFi station power-save mode is set explicitly to **`MIN_MODEM`** (the station sleeps between DTIM beacons) in both apps. This is the mode a typical always-connected ESP32 device runs, so baseline A stays a fair representative of the naive pattern instead of a strawman (`NONE`) or a flattering case (`MAX_MODEM`). It also has to be set explicitly: the Zephyr ESP32 WiFi driver doesn't implement Zephyr's power-save call, so `plant_net` applies the mode with `esp_wifi_set_ps()` after association.

## Considered options

- **Same stack, different architecture** (chosen). It isolates the variable the thesis claims to measure. It fits the methodology's position that differential claims are the core result.
- **The naive pattern as found in the literature, with Zephyr/ESP-IDF defaults.** This is closer to the cited implementations, but some of the measured gap would then come from configuration differences that the thesis can't separate from the architecture effect.
- **Power-save `NONE` or `MAX_MODEM`.** `NONE` keeps the receiver on and inflates baseline A's idle current. `MAX_MODEM` lowers it below a typical device and adds latency that some access points handle badly.

## Consequences

- System power management is part of the architecture under comparison, not of the shared stack. `main` builds with `CONFIG_PM=y`, because it deep-sleeps; baseline A builds with `CONFIG_PM=n`, because it never sleeps. Modem power save must still take effect in baseline A. If the bench trace shows a flat receiver-on idle current instead of the DTIM pattern, this decision needs revisiting.
- Any change to the shared stack applies to both apps, for example static IP or a different keepalive. That is intended: it keeps the comparison clean, and any such change needs its own measurement.
- The thesis (Cap 4.3/4.8) states that baseline A shares `main`'s stack, and records the power-save mode and the access point's DTIM period used on the bench.
- Baseline A's payload workload matches `main`'s: it publishes both `sensors/raw` and `status` every interval with the same encoder. In its `status`, fields that don't apply to it (`wake_reason`, `t_*_ms`) are `null`. It syncs SNTP once at boot rather than every cycle; this is an orchestration difference and is stated as such.

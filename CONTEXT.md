# IoT Plant Monitor Firmware

Firmware for a Bachelor's thesis on the energy profile of a low-power IoT plant-monitoring device. The repo ships one product firmware (`apps/main`) and four baseline-measurement firmwares (`apps/static-sleep`, `apps/active-sensor`, `apps/active-wifi`, `apps/active-ble`) that characterise individual terms of the thesis energy model.

## Language

### Product domain

**Device**:
A single ESP32-C6 board running one of the firmwares in `apps/`. The unit of deployment and the unit of energy measurement.
_Avoid_: node, monitor, plant monitor (the BLE advertised name `PlantMonitor-XXXX` and the MQTT-facing `device_name` are external identifiers, not aliases for the term).

**Cycle**:
One full iteration of the device's operational loop: `Sleep` → wake → `Active phase` → re-enter `Sleep`. Counted by the persistent `device/cycle_count` Settings key.
_Avoid_: wake cycle, sleep/wake cycle.

**Active phase**:
The awake portion of a `Cycle`, from wake to the next sleep-entry. Its measured duration is `t_active`.
_Avoid_: active window (reserved for `BLE advertising window`), wake window.

**Sleep**:
The dormant portion of a `Cycle` — the device is in `PM_STATE_SOFT_OFF`, the radios are off, and only the LP domain (RTC + LP SRAM) is powered. Its measured duration is `t_sleep`.
_Avoid_: deep sleep (use only when contrasting with hypothetical light sleep), suspend, idle.

**Mode**:
The device's top-level state. Two values: `Provisioning mode` and `Operational mode`. The `Cycle` vocabulary is scoped to `Operational mode` only; `Provisioning mode` is instead organised into `Provisioning attempt`s.

**Provisioning mode**:
The mode the device is in when it has no usable WiFi credentials in `Settings`. BLE advertising is on, WiFi and MQTT are off, and the device is waiting for a client to write credentials over the `Provisioning service`.
_Avoid_: PROVISIONING (all caps), provisioning state.

**Operational mode**:
The mode the device is in once credentials are persisted. Runs the sleep/wake `Cycle` indefinitely.
_Avoid_: OPERATIONAL (all caps), normal operation, run mode.

**Provisioning**:
The process of receiving WiFi credentials and other Settings over BLE and persisting them to `Settings`. Happens inside `Provisioning mode` and ends with an atomic `commit` write and a reboot into `Operational mode`. Re-provisioning is the same process, triggered by holding the BOOT button for 3 s during the `Active phase` of a `Cycle`.

**Provisioning attempt**:
One full advertise-and-backoff iteration in `Provisioning mode`: a `Provisioning window` followed by a `Backoff sleep`. Ends when a client successfully writes `commit` (the device then reboots into `Operational mode`); otherwise the next attempt begins automatically. The `Cycle` vocabulary does not apply.
_Avoid_: provisioning cycle, provisioning round, provisioning loop.

**Provisioning window**:
The BLE-on portion of a `Provisioning attempt` (5 min by default), during which the device advertises as `PlantMonitor-XXXX` and accepts writes to the `Provisioning service`. Distinct from the `BLE advertising window`, which is `Operational mode`-scoped.
_Avoid_: advertise phase, provisioning advertise window.

**Backoff sleep**:
The dormant portion of a `Provisioning attempt` (30 min by default), between the end of one `Provisioning window` and the start of the next. The device is in `PM_STATE_SOFT_OFF` drawing `I_sleep`. Distinct from `Sleep`, which is `Cycle`-scoped.
_Avoid_: provisioning sleep, retry sleep, idle sleep.

**BLE advertising window**:
The bounded BLE-on portion of an `Active phase` during which the device advertises the streaming service and accepts a single notification request. Its duration is `ble_window_s` (a `Settings` field); when `ble_window_s` is 0 the window is skipped entirely. Distinct from the `Provisioning window`, which is `Provisioning mode`-scoped.
_Avoid_: BLE window, streaming window, active window.

**Wake reason**:
What caused the device to leave `Sleep` and begin an `Active phase`. Recorded per cycle and published as the `wake_reason` field of the `status` MQTT payload. Two values: `"timer"` (RTC timer fired after `read_interval_s`) and `"soil_moisture_alert"` (LM393 comparator on GPIO6 asserted — soil-moisture threshold crossing). The thesis's central comparison is the energy profile of `"timer"` cycles vs `"soil_moisture_alert"` cycles. The wire string names the *cause*, not the mechanism, so future event sources (e.g. a light-threshold alert) can slot in without retroactively breaking the dataset — see [ADR-0002](docs/adr/0002-wake-reason-wire-string.md).
_Avoid_: wake source, wake type, wake event, `"gpio"` (describes the mechanism, not the cause).

**Settings**:
The device's persistent key-value store, accessed via Zephyr's Settings subsystem and backed by NVS. Holds both user-set configuration (`wifi/ssid`, `wifi/pass`, `device/name`, `device/read_interval_s`, `device/ble_window_s`, `mqtt/broker`) and device-set persistent state (`device/cycle_count`). Both kinds are read via `settings_load` at boot and written via `settings_save_one`. A `commit` write during `Provisioning` atomically persists all fields and reboots into `Operational mode`.
_Avoid_: config, configuration, preferences, NVS (the backing store), settings store, settings entry.

**Sensor reading**:
The per-`Cycle` composite captured during the `Active phase`, modelled in C++ as `struct SensorReading`. Holds all sensor values (HDC1080 temperature & humidity, BMP280 pressure & temperature, BH1750 lux, soil moisture % and raw ADC) plus per-cycle metadata (`wake_reason`, `timestamp_ms`). On publish it is projected onto two MQTT topics: the sensor values go to `plants/{device-name}/sensors/raw`, the metadata plus `Phase duration` measurements and `wifi_rssi_dbm` go to `plants/{device-name}/status`. The name follows the struct; despite "sensor", the value object is broader than just sensor data.
_Avoid_: sample, observation, sensor data, raw payload, reading (unqualified).

### Energy model & measurement

**Energy model**:
The equation `E_cycle = (I_sleep × t_sleep + I_sensor × t_sensor + I_wifi × t_wifi + I_ble × t_ble) × V`. The thesis deliverable. Every measurement and instrumentation choice in the repo exists to populate one of its terms.
_Avoid_: energy equation, power model (power is `I × V`, not the whole model).

**Baseline firmware**:
One of the four apps in `apps/{static-sleep, active-sensor, active-wifi, active-ble}`. Each strips the device down to a single steady-state behaviour so that one `Current draw` term of the `Energy model` can be measured externally with a multimeter. Not tests. Not production code paths. Their sole output is a number measured *off-device*.
_Avoid_: test firmware, measurement app, baseline app.

**Current draw**:
The `I_*` family of variables in the `Energy model` (`I_sleep`, `I_sensor`, `I_wifi`, `I_ble`). Measured *externally* with a multimeter on the corresponding `Baseline firmware`. Treated as a constant per device configuration; not instrumented on-device.
_Avoid_: current consumption (verbose), amperage, draw.

**Phase duration**:
The `t_*` family of variables in the `Energy model` (`t_sleep`, `t_sensor`, `t_wifi`, `t_ble`, `t_active`). Measured *on-device* per `Cycle` in the product firmware (`apps/main`) and published in the `status` MQTT payload. Varies cycle to cycle; the central output of `Operational mode`'s instrumentation.
_Avoid_: phase time, segment duration, t-value.

## Flagged ambiguities

**`Active phase` vs `Energy model` conservation.**
The `Phase duration` terms `t_sensor`, `t_wifi`, `t_ble` do not currently sum to `t_active` exactly — there is unmodelled CPU-on time in the boot prologue (before the first `k_uptime_get()` in `main()`) and the sleep epilogue (cycle-counter persist + retained-RAM write + `pm_state_force` plumbing). The `Energy model` therefore does not strictly conserve over `Cycle` time. The thesis ships `apps/main` with this approximation accepted (~1 % of `E_cycle` in default config) and re-evaluates after the boot-prologue measurement gated in [ADR-0001](docs/adr/0001-active-phase-boundary.md).

## Example dialogue

> **Dev:** I'm seeing `t_active_ms` come in at around 1.3 s on most cycles, but every fifth or so it spikes to 4 s. Should I worry?
>
> **Thesis lead:** What's the `wake_reason` on the slow ones?
>
> **Dev:** Mixed. Both `"timer"` and `"soil_moisture_alert"` cycles do it.
>
> **Thesis lead:** Then it's not the wake path itself. The `Active phase` is `t_sensor + t_wifi + t_ble`; which one's blowing up?
>
> **Dev:** `t_wifi`. From ~600 ms baseline up to 3.5 s on the slow cycles.
>
> **Thesis lead:** Then it's WiFi association variance, not sensor or BLE. Check the `wifi_rssi_dbm` field in the `status` payload on the slow cycles — if they correlate with low RSSI, that's a variance-attribution data point for the thesis, not a bug. Keep them in the dataset.
>
> **Dev:** Got it. While we're here — the `I_wifi` figure I'm multiplying `t_wifi` by, is that from a recent `Baseline firmware` measurement?
>
> **Thesis lead:** It's from `apps/active-wifi` running on the same `Device` with the same `Settings` (same `mqtt/broker`, same `wifi/ssid`). If the deployment moved to a different AP, re-measure — `Current draw` during association can shift with link conditions, and the whole `Energy model` assumes those `I_*` constants are stable for the configuration.
>
> **Dev:** Last thing — I want to flush the `device/cycle_count` for a clean run. Just erase NVS?
>
> **Thesis lead:** Don't say NVS — that's the backing store, not what we're touching. We're clearing a `Settings` key. Hold the BOOT button through the next `Active phase` for 3 s and it triggers re-`Provisioning`, which wipes `Settings` and reboots into `Provisioning mode`. That's the supported way.

## Canonical Portuguese terms (thesis)

The thesis is written in Brazilian Portuguese. These are the canonical PT-BR renderings of the glossary terms above — fix them once and use them consistently across the document. The English terms remain the source of truth; this table only pins the translation so the same concept never appears under two Portuguese names.

| Term (EN) | Termo canônico (PT-BR) | Nota |
|---|---|---|
| Device | dispositivo | — |
| Cycle | ciclo | — |
| Active phase | fase ativa | duração medida: `t_active` |
| Sleep / deep sleep | sono profundo | ancorar "*deep sleep*" entre parênteses na 1ª menção; duração: `t_sleep` |
| Mode | modo | — |
| Provisioning mode | modo de provisionamento | — |
| Operational mode | modo operacional | — |
| Provisioning | provisionamento | — |
| Provisioning attempt | tentativa de provisionamento | — |
| Provisioning window | janela de provisionamento | — |
| Backoff sleep | sono de recuo (*backoff*) | provisório — confirmar |
| BLE advertising window | janela de anúncio BLE | — |
| Wake reason | motivo de despertar | valores em prosa: "despertar por temporizador" / "despertar por alerta de umidade do solo"; strings de fio `"timer"` / `"soil_moisture_alert"` mantidas literais |
| Settings | configurações persistentes (*Settings*) | — |
| Sensor reading | leitura de sensores | — |
| Energy model | modelo de energia | a equação `E_cycle = (I_sleep·t_sleep + I_sensor·t_sensor + I_wifi·t_wifi + I_ble·t_ble)·V` |
| Baseline firmware | firmware de referência | título da seção 4.5: "Firmwares de referência" |
| Current draw | corrente de consumo | família `I_*` |
| Phase duration | duração de fase | família `t_*` |

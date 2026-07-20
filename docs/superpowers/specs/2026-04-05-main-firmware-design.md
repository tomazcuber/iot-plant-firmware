# PRD: Main Application Firmware

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Firmware:** `apps/main`  
**Date:** 2026-04-05  
**Status:** Approved

---

## Purpose

The main application firmware is the thesis deliverable. It implements a low-power IoT node that periodically reads environmental sensors, publishes data to an MQTT broker over WiFi, and optionally streams data to a nearby Android device over BLE. It alternates between deep sleep and active phases, with precise instrumentation of both durations to support the thesis energy model:

```
E_cycle = (I_sleep × t_sleep + I_sensor × t_sensor + I_wifi × t_wifi + I_ble × t_ble) × V
```

Where I_sleep, I_wifi, and I_ble are measured externally using the `static-sleep`, `active-wifi`, and `active-ble` baseline firmwares respectively. The full hardware configuration (DevKitC-1 + all sensors physically connected) shall be used for all baseline measurements.

---

## Platform

| Item | Value |
|---|---|
| MCU | ESP32-C6 |
| Board | ESP32-C6-DevKitC-1 (`esp32c6_devkitc`) |
| RTOS | Zephyr RTOS v4.0 LTS |
| Language | C++ (data modeling, application logic); C (Zephyr drivers) |
| Power Source (primary) | USB 5V — bench measurement |
| Power Source (nice-to-have) | Li-Po 3.7V nominal — simulated deployment |

---

## Sensors

| Sensor | Measurement | Protocol | Zephyr Driver |
|---|---|---|---|
| HDC1080 | Temperature (primary), Humidity | I2C | `ti,hdc` (upstream) |
| BMP280 | Pressure, Temperature (secondary) | I2C | `bosch,bmp280` (upstream) |
| BH1750FVI | Lux / Luminosity | I2C | `rohm,bh1750` (upstream) |
| Resistive Soil Moisture | Soil moisture % | ADC | Direct ADC read (`<zephyr/drivers/adc.h>`) |

All I2C sensors share a single I2C bus. The soil moisture sensor outputs an analog voltage read via the ESP32-C6 ADC peripheral and converted to a percentage via a calibration curve.

Hardware is assembled on a protoboard. No custom PCB.

### Pin Assignment

| Signal | GPIO | Notes |
|---|---|---|
| I2C SDA | GPIO4 | Shared bus — HDC1080, BMP280, BH1750FVI |
| I2C SCL | GPIO5 | Shared bus |
| Soil moisture ADC | GPIO2 | ADC1\_CH2 |
| LM393 DO (deep sleep wake) | GPIO6 | LP domain; RTC GPIO interrupt wake source |

All four signals are on LP-domain GPIOs (GPIO0–GPIO7), which is required for GPIO6 to function as a deep sleep wake source. GPIO4 and GPIO5 are also LP-domain capable but are used here as standard I2C pins via the GPIO matrix. GPIO2 is ADC1\_CH2 and free of I2C and wake assignments.

These assignments shall be defined in `apps/main/app.overlay` and `apps/active-sensor/app.overlay`.

---

## Runtime States

The firmware operates in two top-level states:

```
┌─────────────────┐     no credentials      ┌──────────────────┐
│   PROVISIONING  │◄────────────────────────│   BOOT           │
│   (BLE active)  │                         │   (check NVS)    │
└────────┬────────┘                         └────────┬─────────┘
         │ credentials stored                        │ credentials found
         │                                           ▼
         └──────────────────────────────►  ┌──────────────────┐
                                           │   OPERATIONAL    │
                                           │  (sleep/wake     │
                                           │   cycling)       │
                                           └──────────────────┘
                                                    ▲
                                           button hold │
                                           re-provision│
```

---

## Functional Requirements

### Provisioning

#### FR-MA-01: First-Boot Provisioning Mode
If no WiFi credentials are found in Settings on boot, the device shall automatically enter provisioning mode: BLE advertising begins, WiFi and MQTT are not initialized.

#### FR-MA-02: Re-Provisioning via Button
If the BOOT button (GPIO9, `sw0`) is held for 3 seconds during normal operation, the device shall clear all Settings entries and reboot into provisioning mode.

> **Known limitations:**
> - **Strapping pin behaviour:** GPIO9 is a strapping pin on the ESP32-C6. If the BOOT button is held during a reset event (power cycle or EN press), the ROM bootloader enters download mode instead of booting normally. This is a DevKitC-1-specific behaviour with no impact on normal operation.
> - **No deep-sleep wake on GPIO9:** GPIO9 is not in the ESP32-C6 LP domain (only GPIO0–GPIO7 are LP-capable). The button is therefore physically unable to wake the device from deep sleep. Re-provisioning requires the button to be held *during the active window of a wake cycle*, which is typically only ~1–8 seconds out of every `read_interval_s` (default 300 s, i.e., the button is detectable ~1–3 % of the time). To re-provision in practice, the user should either (1) provision a short `read_interval_s` (e.g., 30 s) before any planned re-provisioning, or (2) wait at the device, repeatedly attempting the 3-second hold until the active window aligns. A production deployment would use a dedicated LP-domain GPIO for re-provisioning.

#### FR-MA-03: BLE Provisioning Service
In provisioning mode, the device shall advertise as `PlantMonitor-XXXX` (where XXXX is the last 4 hex characters of the BLE public address) and expose a GATT provisioning service with the following write characteristics:

> **Implementation note:** The device name is constructed at runtime — `CONFIG_BT_DEVICE_NAME` is a compile-time constant and cannot include a MAC-derived suffix. The firmware shall enable `CONFIG_BT_DEVICE_NAME_DYNAMIC=y` and set `CONFIG_BT_DEVICE_NAME_MAX` to at least `20`. On entering provisioning mode, the firmware reads the BLE public address via `bt_id_get()`, formats the name as `PlantMonitor-XXXX` from the lower two bytes, and applies it via `bt_set_name()` before calling `bt_le_adv_start()`. The same mechanism applies to the operational BLE streaming window (FR-MA-13), which advertises under the same MAC-suffixed name.

| Characteristic | Type | Required | Description |
|---|---|---|---|
| `wifi_ssid` | string | Yes | WiFi network name |
| `wifi_password` | string | Yes | WiFi password |
| `device_name` | string | Yes | Human-assigned node name (used as MQTT topic identifier) |
| `read_interval_s` | uint32 | Yes | Sleep interval in seconds (default: 300) |
| `mqtt_broker` | string | No | Broker address override; if empty, hardcoded default is used |
| `ble_window_s` | uint32 | Yes | BLE advertising window duration in seconds after each WiFi teardown (0 = disable BLE streaming) |
| `commit` | uint8 | Yes | Write any value to atomically persist all fields to Settings and reboot. **No firmware-side validation:** the firmware writes whatever values are currently in RAM. Field validation (e.g., non-empty SSID) is the Android app's responsibility. |

#### FR-MA-04: Provisioning Timeout and Backoff
If no BLE client connects within 5 minutes of entering provisioning mode, the device shall stop BLE advertising and enter deep sleep for 30 minutes. On wake from this backoff sleep, it shall re-enter provisioning mode (BLE advertising) for another 5-minute window. This cycle repeats indefinitely until a client successfully completes provisioning.

**Rationale:** A tight reboot-and-retry loop wastes energy continuously advertising BLE in scenarios where the user has stepped away or forgotten the device. The backoff sleep conserves battery in deployment scenarios while still allowing the user to provision whenever they return with the Android app. Provisioning is a one-time event per deployment and is not part of the energy model.

#### FR-MA-05: Settings Defaults on First Boot
On first boot (Settings store empty), the firmware shall write the following defaults via `settings_save_one` before entering provisioning mode:

| Settings Key | Default |
|---|---|
| `wifi/ssid` | `""` (empty) |
| `wifi/pass` | `""` (empty) |
| `device/name` | `"plant"` |
| `device/read_interval_s` | `300` |
| `device/ble_window_s` | `5` |
| `mqtt/broker` | `""` (empty — falls back to compile-time default) |
| `device/cycle_count` | `0` |

This guarantees that any successful `commit` (whether the user wrote all characteristics or only some) produces a structurally valid configuration. The Android app is responsible for validating that user-entered values (especially `wifi/ssid` and `wifi/pass`) are usable before triggering `commit`.

#### FR-MA-05a: Settings Schema
The following fields shall be persisted via the Zephyr Settings subsystem (`CONFIG_SETTINGS=y`, `CONFIG_SETTINGS_NVS=y`). All reads use `settings_load` at boot; all writes use `settings_save_one`.

| Settings Key | Type | Description |
|---|---|---|
| `wifi/ssid` | string | WiFi network name |
| `wifi/pass` | string | WiFi password |
| `device/name` | string | Human-assigned node name |
| `device/read_interval_s` | uint32 | Sleep interval in seconds |
| `mqtt/broker` | string | Broker address override (empty = use default) |
| `device/ble_window_s` | uint32 | BLE advertising window duration in seconds (0 = disabled) |
| `device/cycle_count` | uint32 | Persistent cycle counter, survives deep sleep |

---

### Operational Wake Cycle

#### FR-MA-06: Wake Sources
The device shall support two wake sources from deep sleep:

- **RTC timer** — fires after the provisioned `read_interval_s` elapses
- **RTC GPIO interrupt** — fires when the soil moisture sensor's LM393 comparator digital output (DO pin) asserts, indicating the soil moisture has crossed the threshold set by the sensor module's onboard potentiometer. The DO pin is wired to **GPIO6** (LP domain; see Pin Assignment).

Both wake sources trigger the same wake cycle sequence. The wake reason is recorded and published.

Before entering deep sleep, the firmware shall write the following fields to Zephyr retained RAM (`zephyr,retained-ram`, enabled via `CONFIG_RETAINED_MEM=y`):

| Field | Type | Description |
|---|---|---|
| `magic` | uint32 | Validity marker; `0xDEADB33F` when struct is valid. Any other value means the struct is uninitialised (first boot, reflash, or power loss during sleep). |
| `sleep_start_rtc_ms` | int64 | RTC clock value at sleep entry, used to compute actual sleep duration on GPIO wake |
| `scheduled_sleep_ms` | uint32 | `read_interval_s × 1000`, used directly for timer wake |

On wake, the firmware shall first check `magic`:
- If `magic != 0xDEADB33F`: retained RAM is invalid. Set `t_sleep_ms = null`. Do not attempt to compute sleep duration.
- If `magic == 0xDEADB33F`: retained RAM is valid. Compute `t_sleep_ms` as:
  - **Timer wake:** `scheduled_sleep_ms` (exact, no RTC read required)
  - **GPIO wake:** `rtc_now_ms - sleep_start_rtc_ms` (actual elapsed time)

> **Implementation note:** Retained RAM is LP SRAM that remains powered during deep sleep. Its quiescent current is already included in the I_sleep measurement (the static-sleep firmware measures the whole board, including the LP domain). There is no additional power overhead relative to using retained RAM vs not using it.

> **Thesis note:** This dual-source design enables direct comparison of timer-wake vs event-wake energy profiles in the thesis analysis, as each cycle publishes its wake reason.

#### FR-MA-07: Wake Cycle Sequence
On each wake, the firmware shall execute the following sequence in order:

1. Record `t_active_start` timestamp (Zephyr `k_uptime_get()`)
2. Record wake reason (timer or GPIO interrupt)
2a. Check retained RAM `magic`; compute `t_sleep_ms` if valid, otherwise set `t_sleep_ms = null`
3. Power on I2C bus; read all sensors sequentially (BMP280 → BH1750FVI → HDC1080 → soil moisture ADC)
4. Power down I2C bus and ADC
4a. Record `t_wifi_start` timestamp (Zephyr `k_uptime_get()`)
5. Connect to WiFi (10-second timeout — see DD-MA-03)
6. Synchronise wall-clock time via SNTP (3-second timeout — see DD-MA-11). On failure, set `timestamp_ms = null` and continue the cycle normally.
7. Connect to MQTT broker
8. Publish `sensors/raw` payload
9. Capture `wifi_rssi_dbm` from the WiFi interface status (single sample, post-publish, pre-teardown — see DD-MA-13)
10. Publish `status` payload
11. Disconnect MQTT; disconnect WiFi
12. Record `t_ble_start` timestamp
13. Start BLE advertising (`ble_window_s` window; skipped if 0)
14. If BLE client connects: send `sensor_data` notification with latest reading; disconnect
15. Stop BLE advertising
16. Record `t_active_end` timestamp
17. Compute `t_wifi_ms`, `t_ble_ms`, `t_active_ms`
18. Persist incremented `device/cycle_count` to Settings
19. Write `sleep_start_rtc_ms`, `scheduled_sleep_ms`, and `magic = 0xDEADB33F` to retained RAM
20. Enter deep sleep for `read_interval_s` seconds

#### FR-MA-08: Sensor Data Model

```cpp
namespace plant {

enum class WakeReason {
    Timer,
    SoilMoistureAlert,
};

struct SensorReading {
    float temperature_c;                        // HDC1080 (primary)
    float bmp280_temperature_c;                 // BMP280 (secondary, redundant)
    float humidity_pct;                         // HDC1080
    float pressure_hpa;                         // BMP280
    float lux;                                  // BH1750FVI
    float soil_moisture_pct;                    // Resistive ADC sensor, calibrated
    uint16_t soil_moisture_raw_adc;             // Raw ADC value, pre-calibration
    std::optional<int64_t> timestamp_ms;        // Wall-clock time (SNTP-synced); nullopt on SNTP failure
    WakeReason wake_reason;
};

} // namespace plant
```

HDC1080 is the primary temperature source due to its higher precision. BMP280 temperature is published as a secondary reading for redundancy.

> **Implementation note:** The HDC1080 requires a settling time (~6.5 ms in default 14-bit mode) between temperature and humidity reads. This is a driver-level concern handled by the upstream `ti,hdc` Zephyr driver and does not affect the wake cycle sequence at the spec level.

#### FR-MA-08a: Sensor Failure Handling
Each sensor read shall be attempted exactly once per cycle. No retries. On I2C bus failure, sensor NACK, timeout, or any driver-level read error, the corresponding field in the `sensors/raw` payload shall be published as JSON `null`. The cycle shall continue normally — `status` is published, `wifi_rssi_dbm` and timing data are captured, BLE advertising proceeds.

**Rationale:** A failed sensor read is itself informative data — it indicates a hardware issue (loose wire, dead sensor) that the operator needs to see in the dataset. Retries inflate `t_wifi_ms` ambiguously, hide hardware problems, and do not improve data quality on persistent faults. Aborting the entire cycle would also discard the still-valid timing, RSSI, and other-sensor data. Publishing `null` for failed sensors and continuing preserves maximum information per cycle and makes failures visible during post-processing.

---

### MQTT

#### FR-MA-09: Topic Structure

| Topic | Direction | Description |
|---|---|---|
| `plants/{device-name}/sensors/raw` | Publish | Full sensor reading |
| `plants/{device-name}/status` | Publish | Device health and cycle metrics |

#### FR-MA-10: Payload Schemas

`plants/{device-name}/sensors/raw`:
```json
{
  "temperature_c": 23.4,
  "bmp280_temperature_c": 23.1,
  "humidity_pct": 61.2,
  "pressure_hpa": 1013.5,
  "lux": 450.0,
  "soil_moisture_pct": 65.0,
  "soil_moisture_raw_adc": 1820,
  "timestamp_ms": 1712000000000
}
```

`timestamp_ms` shall be JSON `null` if SNTP synchronisation failed for this cycle. All other fields are unaffected.

`plants/{device-name}/status`:
```json
{
  "wake_reason": "timer",
  "t_active_ms": 1240,
  "t_sensor_ms": 120,
  "t_wifi_ms": 860,
  "t_ble_ms": 260,
  "t_sleep_ms": 300000,
  "wifi_rssi_dbm": -62,
  "cycle_count": 42,
  "timestamp_ms": 1712000000000
}
```

Nullable fields:
- `timestamp_ms` — JSON `null` if SNTP synchronisation failed this cycle
- `t_sleep_ms` — JSON `null` if retained RAM was uninitialised on wake (first boot, reflash, or power loss during sleep). Occurs at most once per power cycle; all subsequent cycles will have a valid value.

All other fields are always published.

> **Invariant:** `t_active_ms == t_sensor_ms + t_wifi_ms + t_ble_ms`. A violation indicates a firmware instrumentation bug. When `ble_window_s` is 0, `t_ble_ms` shall be published as 0.

#### FR-MA-11: MQTT QoS
All publishes shall use QoS level 0 (fire and forget). See DD-MA-02.

#### FR-MA-12: MQTT Broker Configuration
A hardcoded default broker address shall be compiled into the firmware. If a non-empty `mqtt/broker` value exists in Settings, it shall override the default.

---

### BLE Streaming

#### FR-MA-13: Opportunistic BLE Streaming
After WiFi teardown, the device shall advertise a GATT streaming service for a window of `ble_window_s` seconds (provisioned via BLE, stored in NVS). If `ble_window_s` is 0, BLE advertising shall be skipped entirely and the device shall proceed directly to deep sleep. If a client connects and subscribes to the `sensor_data` characteristic, the firmware shall send a single notification containing the latest `SensorReading` as JSON, then disconnect. If no client connects within the window, advertising stops immediately.

#### FR-MA-14: BLE Streaming Service

| Characteristic | Properties | Description |
|---|---|---|
| `sensor_data` | Read, Notify | Latest sensor reading as JSON |

#### FR-MA-15: BLE and WiFi Non-Overlap
BLE and WiFi shall not be active simultaneously. BLE advertising shall begin only after WiFi and MQTT are fully torn down. This ensures predictable current draw during each radio phase.

---

### GATT UUID Registry

These UUIDs are the shared contract between the firmware and the Android app. Neither side may define them independently. All are 128-bit custom UUIDs.

#### Provisioning Service

| Role | UUID |
|---|---|
| **Provisioning Service** | `de117241-856f-4863-89bc-d0d1af61af2e` |
| `wifi_ssid` characteristic | `f83fb18d-6564-4dae-b637-3de00ecb3e1a` |
| `wifi_password` characteristic | `1d7ad073-137b-4252-aa53-813c9b582c5c` |
| `device_name` characteristic | `82074945-1e99-4212-98c6-408eb2f5508d` |
| `read_interval_s` characteristic | `eb75998e-ca90-4acb-ba97-3084bf5b600a` |
| `ble_window_s` characteristic | `328f0e49-bdcd-4ab0-a4a0-6b74cf619f86` |
| `mqtt_broker` characteristic | `a150dad3-7af5-4488-bf8e-676f12d8b4d6` |
| `commit` characteristic | `356057e7-07fb-4cdf-8c2e-ebd448d84584` |

#### Streaming Service

| Role | UUID |
|---|---|
| **Streaming Service** | `2a5b4897-2a20-4096-83c6-a15ff4d4c22a` |
| `sensor_data` characteristic | `1244312e-e41d-413e-9767-d9e86b9d2648` |

> **Note for the Android app:** The device advertises the Streaming Service UUID in its advertisement packet during the operational BLE window, and the Provisioning Service UUID during provisioning mode. The app should scan for each UUID to distinguish device state without requiring a connection first.

---

### Energy Instrumentation

#### FR-MA-16: Active Phase Instrumentation
The firmware shall capture four timestamps using Zephyr's `k_uptime_get()`:

- `t_active_start` — at wake entry (step 1 of the wake cycle)
- `t_wifi_start` — after all sensors are read and peripherals are powered down, immediately before WiFi initialisation (step 4a)
- `t_ble_start` — immediately after WiFi and MQTT teardown, before BLE advertising begins (step 12, after step 11)
- `t_active_end` — after BLE advertising stops, before deep sleep entry (step 16)

The following durations shall be computed and published in the `status` topic every cycle:

- `t_sensor_ms = t_wifi_start - t_active_start`
- `t_wifi_ms = t_ble_start - t_wifi_start`
- `t_ble_ms = t_active_end - t_ble_start` (0 when `ble_window_s` is 0)
- `t_active_ms = t_active_end - t_active_start`

#### FR-MA-17: Cycle Counter
A `device/cycle_count` persisted in Settings shall increment on every wake and be published in the `status` topic. This enables detection of missed MQTT publishes (gaps in sequence) and correlation of data with specific cycles in thesis analysis.

---

## Non-Functional Requirements

### NFR-MA-01: Deep Sleep Between Cycles
The device shall enter Zephyr-managed deep sleep between cycles. All peripherals, WiFi stack, and BLE stack shall be fully powered down before sleep entry.

### NFR-MA-02: Minimise t_active
The wake cycle sequence shall be optimised to minimise `t_active`. Specifically: peripherals shall be powered down before WiFi initialisation (reducing simultaneous current draw), and the BLE advertising window shall be bounded (5 seconds maximum) regardless of client activity.

### NFR-MA-03: Reproducible Measurements
The firmware shall produce consistent, reproducible `t_active_ms` values across cycles under identical network conditions, enabling statistical analysis in the thesis.

### NFR-MA-04: Build System Integration
The firmware shall build within the shared west workspace at the repository root, targeting the upstream `esp32c6_devkitc` board. Hardware-specific devicetree configuration (I2C sensor nodes, ADC channel for soil moisture, LP GPIO for deep sleep wake) shall be provided via `apps/main/app.overlay`.

---

## Kconfig

```
CONFIG_WIFI=y
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_GATT=y
CONFIG_BT_DEVICE_NAME_DYNAMIC=y
CONFIG_BT_DEVICE_NAME_MAX=20
CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_MQTT_LIB=y
CONFIG_CPP=y
CONFIG_STD_CPP17=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_RETAINED_MEM=y
CONFIG_RTC=y
CONFIG_SNTP=y
```

---

## Design Decisions

### DD-MA-01: Periodic + Event-Driven Wake (Hybrid Approach)
The firmware supports both RTC timer wake (periodic) and RTC GPIO wake (soil moisture interrupt).

**Rationale:** Periodic readings are a hard requirement. Event-driven wake is a thesis design variable: the dual-source design enables direct energy comparison between timer-triggered and event-triggered cycles using the same firmware, published data, and energy formula. This strengthens the thesis analysis without requiring separate firmware builds.

**Thesis implication:** Each `status` publish includes `wake_reason`, enabling automated classification of cycle types in post-processing.

### DD-MA-02: MQTT QoS 0 and Discard-on-Failure
On WiFi connection failure (timeout after 10 seconds), the current cycle's sensor data is discarded and the device returns to deep sleep. MQTT publishes use QoS 0.

**Rationale:** For plant monitoring, data consistency on network failure is low priority. Plants change slowly — soil moisture, temperature, and humidity shift over hours. A gap in the time-series caused by a WiFi outage is visible by timestamp but does not compromise plant health decisions or invalidate the dataset. QoS 0 minimises `t_active` by eliminating broker acknowledgement round-trips. Implementing local buffering (QoS 1/2 or NVS replay) would introduce non-uniform cycle energy profiles, complicating the thesis energy model.

**Thesis implication:** The methodology chapter should note that occasional data gaps due to network failure are expected and acceptable for this monitoring domain. Gaps are detectable via `cycle_count` discontinuities in the published data.

### DD-MA-03: WiFi Connection Timeout (10 seconds)
WiFi connection attempts are bounded to 10 seconds before the cycle is aborted.

**Rationale:** An unbounded retry loop would produce anomalous `t_active_ms` values that skew the energy model. A fixed timeout ensures every failed cycle has a predictable, bounded energy cost that can be excluded from analysis or treated as a distinct data point.

### DD-MA-04: Reading Interval Configurable via BLE Provisioning
The sleep interval is set during BLE provisioning and stored in NVS. It cannot be changed remotely via MQTT.

**Rationale:** Configurable interval makes the device practical for thesis demonstrations (different intervals for different experiments without reflashing) while avoiding the complexity of remote configuration via MQTT, which would require the device to subscribe to a command topic and handle interval changes mid-deployment.

**Thesis implication:** When comparing energy profiles across different sleep intervals, the device must be re-provisioned via the Android app. Document this as the configuration procedure in the thesis appendix.

### DD-MA-05: Device Identity via Human-Assigned Name
Each device is assigned a human-readable name during BLE provisioning (e.g., `balcony-pot-1`). This name is used as the MQTT topic identifier.

**Rationale:** In a distributed monitoring scenario with multiple nodes, human-readable topic names make broker-side configuration, debugging, and data analysis significantly easier than MAC-address-based identifiers. The name is unique within a deployment by user convention.

### DD-MA-06: JSON Payload Format
Sensor data is encoded as JSON for MQTT and BLE payloads.

**Rationale:** Compact binary formats (e.g., Protocol Buffers) were evaluated. For this sensor frequency and payload size (~120 bytes), the transmission time difference between JSON and Protobuf is in the order of microseconds at WiFi speeds — negligible relative to connection establishment overhead. JSON was chosen for its simplicity across firmware, broker subscribers, and the Android client, without any meaningful energy trade-off.

### DD-MA-07: Configurable BLE Advertising Window
The BLE advertising window duration (`ble_window_s`) is provisioned via the Android app and stored in NVS. A value of 0 disables BLE streaming entirely.

**Rationale:** A fixed BLE window prevents thesis experiments from isolating WiFi-only vs WiFi+BLE energy profiles without reflashing. Making it configurable via provisioning enables a factorial experiment design (interval × BLE on/off) reachable entirely through the Android app. This is a direct thesis contribution: the firmware supports controlled energy comparison across configurations without firmware changes.

**Thesis implication:** Present results as a 2×3 matrix (read intervals × BLE on/off). The `ble_window_s = 0` condition establishes WiFi-only energy per cycle; non-zero values show the BLE overhead as a function of window duration.

### DD-MA-09: BLE and WiFi Time-Division Multiplexing
BLE and WiFi are never active simultaneously within a wake cycle.

**Rationale:** While the ESP32-C6 supports WiFi/BLE coexistence, running both simultaneously increases instantaneous current draw and makes `t_active` harder to model. Sequential operation (WiFi phase → BLE phase) produces cleaner energy profiles and simplifies the energy model: `t_active = t_wifi + t_ble`.

### DD-MA-11: SNTP Wall-Clock Sync Every Cycle
The firmware shall query an SNTP server on every wake cycle, between WiFi connect and MQTT publish. Default server: `pool.ntp.org`. Timeout: 3 seconds. On SNTP failure, `timestamp_ms` is set to `null` and the cycle continues normally — sensor data, timing, RSSI, and `cycle_count` are all published.

**Rationale:** The ESP32-C6 has no battery-backed RTC and `k_uptime_get()` resets on every deep sleep wake. The DevKitC-1 lacks an external 32.768 kHz crystal, so the internal RC oscillator drifts ~5%/day — making local RTC-based wall-clock tracking impractical between syncs. SNTP every cycle adds ~20–100ms to `t_wifi_ms` and ~0.05 mJ per cycle (~0.007% of cycle energy at 300s sleep interval), which is unmeasurable noise. In exchange, most published records carry a valid wall-clock timestamp.

On SNTP failure, aborting the cycle would discard valid sensor readings, timing data, and RSSI — all of which are useful regardless of wall-clock time. This is inconsistent with the approach taken for sensor failures (FR-MA-08a), which publishes `null` for the failed field and continues. SNTP failure is treated the same way: the affected field (`timestamp_ms`) is published as `null`, and the rest of the record is preserved. Approximate timestamps for null-timestamp records can be reconstructed in post-processing using `cycle_count` and `read_interval_s`.

**Thesis implication:** The methodology chapter should note that SNTP latency is included in `t_wifi_ms` and is a small but real component of the measured WiFi-phase duration. Records with `null` timestamp are identifiable in the dataset and can be reconstructed or excluded as appropriate for the analysis.

### DD-MA-12: Soil Moisture Calibration via Kconfig, Raw ADC Always Published
The soil moisture percentage is computed from the raw ADC value using a two-point linear calibration: `soil_moisture_pct = 100 × (CONFIG_SOIL_DRY_ADC - raw_adc) / (CONFIG_SOIL_DRY_ADC - CONFIG_SOIL_WET_ADC)`. The two reference points are exposed as Kconfig values (`CONFIG_SOIL_DRY_ADC`, `CONFIG_SOIL_WET_ADC`) defined in `apps/main/Kconfig` with defaults set in `apps/main/prj.conf`. They are characterised once per probe by measuring the ADC value with the probe in dry air and submerged in water, then updating `prj.conf` (or overriding via `west build -t menuconfig`) and rebuilding. The raw ADC value is always published in `sensors/raw` alongside the calibrated percentage.

**Rationale:** Storing calibration in Settings would require an additional provisioning flow (sensor characterisation via Android app) which is out of scope for the thesis. Kconfig values are simple, version-controlled, inspectable via `menuconfig`, and adequate for a single-sensor thesis demo. Publishing the raw ADC value makes the dataset re-calibratable in post-processing — if the calibration constants are later found to be wrong (e.g., dry-baseline measured with a damp probe, probe replacement after corrosion), the percentage can be recomputed without re-collecting data. This is a one-line cost in payload size (~5 bytes) for permanent dataset insurance.

**Thesis implication:** The methodology chapter should record the `CONFIG_SOIL_DRY_ADC` and `CONFIG_SOIL_WET_ADC` values used during the experiment, alongside the probe model and VCC voltage at characterisation time. Any post-hoc recalibration should be presented alongside the original calibration, with both percentages derivable from the published raw ADC.

### DD-MA-13: WiFi RSSI Capture as Variance Attribution Variable
The firmware shall capture `wifi_rssi_dbm` once per cycle, immediately after MQTT publish completes and before WiFi teardown. Single sample, no averaging. The value is published in the `status` topic.

**Rationale:** The thesis energy model treats `t_wifi_ms` as a per-cycle variable. In any real deployment, `t_wifi_ms` will vary across cycles — typically ranging from ~700ms on a clean network to ~3000ms+ on a marginal one — primarily due to WiFi association latency, which is itself driven by signal quality. Without RSSI in the dataset, this variance is unexplained and the thesis can only report mean and standard deviation of `t_wifi_ms` without attribution. With RSSI:

1. **Variance attribution:** A scatter plot of `t_wifi_ms` vs `wifi_rssi_dbm` typically shows a clear inverse correlation. Reporting the regression coefficient turns "noise" in the energy model into "an explained relationship between signal quality and active-phase energy."
2. **Defensible outlier exclusion:** Cycles with RSSI significantly worse than baseline can be excluded as environmental anomalies with a documented reason, rather than discarded by judgment.
3. **Reproducibility context:** Reporting "mean t_wifi_ms = 980ms ± 120ms at RSSI -62 ± 5 dBm" makes results interpretable and reproducible by another researcher in a different RF environment.
4. **Deployment story:** For the live demo, comparing the home plant (e.g., RSSI -70 dBm, longer t_wifi) against the local plant (e.g., RSSI -50 dBm, shorter t_wifi) provides concrete evidence that the model captures real deployment effects.

The capture point (post-publish, pre-teardown) was chosen because the radio has been actively transmitting and the measurement is most representative of the connection quality experienced during the cycle.

**Thesis implication:** The methodology chapter should explicitly include RSSI as a controlled/observed variable. Results sections reporting energy or `t_wifi_ms` should be accompanied by the RSSI distribution from the same cycles. A scatter plot of `t_wifi_ms` vs `wifi_rssi_dbm` is recommended as a thesis figure to demonstrate the relationship.

### DD-MA-14: Power Source
Primary measurement platform: USB 5V (bench measurement). The onboard LDO regulator is part of the measured system — all current measurements include regulator quiescent current.

**Nice-to-have:** Li-Po 3.7V nominal, powering the board directly or via a small LDO, for simulated deployment energy measurements. This would require a separate measurement run with `V = 3.7V` in the energy formula.

**Thesis implication:** The primary thesis results use V = 5V (USB bench measurement). If Li-Po measurements are included, they should be presented as a separate scenario with V = 3.7V.

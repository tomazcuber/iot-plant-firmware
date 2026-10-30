# PRD: Active WiFi Firmware (Comparison Baseline A)

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Firmware:** `apps/active-wifi`  
**Date:** 2026-04-05  
**Status:** Revised 2026-10-30: requirements rewritten for comparison baseline A on the [shared modules](2026-10-30-shared-modules-design.md). Not implemented.

> **History.** Until 2026-10-15 this firmware measured I_wifi with a multimeter, using a tight connect–publish–disconnect loop. With the INA219 bench, I_wifi and t_wifi come from the current trace of the `main` firmware itself, which also removes the warm-versus-cold reconnect approximation of the former DD-AW-03. This firmware was then repurposed as **comparison baseline A**. On 2026-10-30 its requirements were rewritten to match, on top of the shared modules.

---

## Purpose

Comparison baseline A implements the naive always-connected pattern reported in the literature. WiFi stays associated, and the device reads the sensors, publishes, and waits in a loop without ever sleeping. Examples are Shin et al. (2024), Pamudji et al. (2026) and the no-sleep condition of Dzahir and Chia (2023). Its energy per publication is compared against `main`'s energy per cycle at the same interval.

The comparison isolates the **architecture** (always connected vs deep-sleep cycles). Baseline A therefore uses exactly the same sensor read, payload encoder, WiFi/MQTT/SNTP primitives and network settings as `main`; only the orchestration differs ([ADR-0006](../../adr/0006-comparison-baseline-same-stack.md)).

Baseline A is not a `Baseline firmware`. It populates no `Current draw` term of the energy model. Its output is its own energy per publication, `I_A × V × interval`.

---

## Platform

| Item | Value |
|---|---|
| MCU | ESP32-C6 |
| Board | ESP32-C6-DevKitC-1 (`esp32c6_devkitc`) |
| RTOS | Zephyr RTOS v4.3.1 |
| Language | C |

---

## Functional Requirements

### FR-AW-01: Persistent Connection Loop
The firmware shall:

1. Call `plant_sensors_init()` and `plant_net_init()`.
2. Connect to WiFi (`plant_wifi_connect`) and to the MQTT broker (`plant_mqtt_connect`, client ID = `device_name`).
3. Synchronise wall-clock time once with `plant_sntp_sync`. On failure, `timestamp_ms` is `null` for every publication until the next boot.
4. Loop forever:
   1. Read all sensors with `plant_sensors_read()`, in the same order as `main` (FR-MA-07 step 3).
   2. Publish `sensors/raw` (FR-AW-02).
   3. Sample RSSI with `plant_wifi_rssi`.
   4. Publish `status` (FR-AW-02).
   5. Wait until the next publication is due, calling `plant_mqtt_poll` so keepalive and incoming packets are handled.

The connection is never torn down deliberately, and the device never enters a sleep state (NFR-AW-01).

### FR-AW-02: Payloads and Topics
Both topics are published every interval, with the shared `plant_json` encoder, at QoS 0, under `device_name = "baseline-a"`:

| Topic | Payload |
|---|---|
| `plants/baseline-a/sensors/raw` | Same schema and encoding as `main` (FR-MA-10). Failed sensors are `null` (FR-MA-08a). `timestamp_ms` = boot epoch + `k_uptime_get()`, or `null` if the boot sync failed. |
| `plants/baseline-a/status` | Same schema as `main`. `wifi_rssi_dbm` is sampled each publication. `cycle_count` is a publication counter held in RAM (reset at boot), so gaps stay detectable. `timestamp_ms` is as above. `wake_reason`, `t_active_ms`, `t_sensor_ms`, `t_wifi_ms`, `t_ble_ms` and `t_sleep_ms` are `null`. |

Publishing both topics keeps the per-publication radio workload equal to `main`'s.

### FR-AW-03: Publication Interval
The publication interval shall equal `main`'s `read_interval_s` for each interval in the experiment matrix. It is an app-local compile-time Kconfig value (`CONFIG_APP_PUBLISH_INTERVAL_S`), rebuilt per condition. Each publication is scheduled from the previous one's start time, so the interval does not drift by the read and publish duration.

### FR-AW-04: Failure Handling
- If WiFi association, DHCP or the MQTT connection fails at boot, or `plant_mqtt_poll` reports a dropped connection, the firmware shall reconnect immediately: WiFi first if it is down, then MQTT. There is no backoff and no sleep.
- A publication that falls due while disconnected is skipped. The publication counter still increments, so the gap is visible in `cycle_count`.
- A failed sensor is published as `null`, as in `main`.

### FR-AW-05: Compile-Time Credentials
WiFi SSID, WiFi password and the MQTT broker host shall be app-local Kconfig strings (`CONFIG_APP_WIFI_SSID`, `CONFIG_APP_WIFI_PSK`, `CONFIG_APP_MQTT_BROKER`), set in a gitignored `credentials.conf`. They are passed as arguments to the `plant_net` primitives. There is no runtime provisioning. The broker port, keepalive and every other network setting come from the shared `plant-net` snippet.

---

## Non-Functional Requirements

### NFR-AW-01: No Sleep States
All Zephyr power management shall be disabled (`CONFIG_PM=n`). The MCU never enters light or deep sleep. Between publications the CPU idles in Zephyr's idle thread. WiFi modem power save (`MIN_MODEM`, shared with `main`) is a radio-level mechanism and stays enabled: it is part of the shared stack, not a system sleep state.

### NFR-AW-02: Minimal Logging
Serial logging shall be kept at the minimum level necessary for debugging connection issues. Excessive logging can skew current draw measurements.

### NFR-AW-03: Single Thread Architecture
The firmware shall use a single Zephyr application thread running the loop in FR-AW-01. The shared modules start no threads of their own.

### NFR-AW-04: Build System Integration
The firmware shall build within the shared west workspace at the repository root, targeting the upstream `esp32c6_devkitc` board. `apps/active-wifi/CMakeLists.txt` sets `SHIELD plant_sensors` and `SNIPPET plant-net` before `find_package(Zephyr)`. These provide the sensor devicetree, the sensor and network Kconfig, and the soil calibration. No `app.overlay` is required. The app shall not set any shared symbol in its `prj.conf` (shared-modules spec SM-05).

---

## Kconfig

App-specific symbols only (`apps/active-wifi/prj.conf`). Sensor and network symbols come from the shield and the snippet.

```
CONFIG_BT=n
CONFIG_PM=n
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2
```

`apps/active-wifi/Kconfig` defines `APP_PUBLISH_INTERVAL_S`, `APP_WIFI_SSID`, `APP_WIFI_PSK` and `APP_MQTT_BROKER`, then sources `Kconfig.zephyr`.

---

## Measurement Procedure

This procedure shall be documented in the thesis methodology chapter. Revised 2026-10-15: it measures this firmware as comparison baseline A.

### Bench

- Board under test: MuseLab nanoESP32-C6, with the HDC1080, BMP280, BH1750FVI and the resistive soil moisture sensor (with its LM393 comparator module) connected and powered as in deployment.
- Power: a second board connected to the computer by USB acts as the 5 V source. The board under test is powered only through its 5 V pin, via the measurement path. All grounds are common.
- Neither USB-C port of the board under test may be connected during a measurement: a USB cable would power the board directly and bypass the measurement path. Connect USB only to flash, then disconnect it.
- Power source voltage: V = 5 V.
- Same access point as the `main` runs. Record its DTIM period.

### Steps

1. Set `CONFIG_APP_PUBLISH_INTERVAL_S` for the condition under test, build, flash `apps/active-wifi` and disconnect USB.
2. Insert the INA219 in series in the 5 V line. Log at the same sampling settings used for the `main` trace.
3. Power the board and wait for the first publication on the broker.
4. Log at least 10 complete publication cycles per run; repeat three runs per condition.
5. Record the mean current over an integer number of cycles as **I_A**, and energy per cycle as I_A × V × interval.
6. From the same trace, record the mean current between publications as **I_wifi,idle** (WiFi associated, no traffic). This term is needed to express baseline A in the energy model, which otherwise has no state for an associated but idle radio.
7. Sanity check: the reduction from baseline A to `main` with `ble_window_s` = 0 at a 60 s interval should fall near the 72–83% range reported by Mayo et al. (2026), Dzahir and Chia (2023) and Smyrilios et al. (2025). A result far outside that range points to a bench or firmware problem.

---

## Design Decisions

### DD-AW-01: Zephyr for All Firmwares
See DD-SS-01 in the Static Sleep Firmware PRD. Baseline A runs on the same RTOS, with the same OS overhead, as the firmware it is compared against.

### DD-AW-02: BLE Disabled
BLE is disabled in this firmware.

**Rationale:** The always-connected pattern in the cited literature is WiFi only. Baseline A is compared against `main` with `ble_window_s = 0`. Comparisons with BLE enabled add `main`'s BLE energy on top, which is measured separately (I_ble × t_ble).

### DD-AW-03: Persistent Connection
The firmware connects once and stays connected, publishing every interval. This replaces the former tight reconnect loop.

**Rationale:** The tight reconnect loop existed to make a multimeter average over `main`'s connection-establishment transients when measuring I_wifi. The INA219 trace of `main` now provides I_wifi directly. Baseline A's role is different: it represents how an always-connected device behaves, and such a device does not reconnect between publications.

### DD-AW-04: Same Stack as `main`
Sensor read, payload encoding, WiFi/MQTT/SNTP primitives and every network setting (power-save `MIN_MODEM`, DHCP, MQTT keepalive 60 s, client ID = `device_name`) are shared with `main` through the shared modules. Only the orchestration differs. See [ADR-0006](../../adr/0006-comparison-baseline-same-stack.md).

### DD-AW-05: Payload Size Has Negligible Power Impact
JSON was chosen over compact binary formats (e.g., Protocol Buffers) for payload encoding.

**Rationale:** At WiFi transmission speeds, the difference between a ~120-byte JSON payload and a ~30-byte Protobuf payload corresponds to microseconds of additional transmission time — well below the noise floor of the energy model. JSON was therefore chosen for its simplicity across firmware, broker, and Android client.

**Thesis implication:** When discussing payload format in the paper, note that payload encoding was evaluated and found to have negligible impact on E_cycle for this sensor frequency and payload size.

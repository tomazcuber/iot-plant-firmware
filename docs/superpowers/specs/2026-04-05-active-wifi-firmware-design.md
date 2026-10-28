# PRD: Active WiFi Firmware

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Firmware:** `apps/active-wifi`  
**Date:** 2026-04-05  
**Status:** Revised (2026-10-15): repurposed as comparison baseline A; requirement rewrite pending

> **Change of role.** With the INA219 bench, I_wifi and t_wifi are obtained from the current trace of the `main` firmware itself, which also removes the warm-versus-cold reconnect approximation acknowledged in DD-AW-03. This firmware therefore no longer characterises I_wifi. It is repurposed as **comparison baseline A**: the naive always-connected pattern reported in the literature (Wi-Fi kept associated, read–publish–wait loop, no sleep), as in Shin et al. (2024), Pamudji et al. (2026) and the no-sleep condition of Dzahir and Chia (2023).
>
> Changes required before measurement (not yet applied to the requirements below):
> - FR-AW-01 and DD-AW-03: replace the reconnect loop with a persistent connection (connect once, then read, publish and wait in a loop). The implementation plan's Task 5 code already follows this structure.
> - FR-AW-03: read the real sensors in the same order as `main` (FR-MA-07, step 3) instead of publishing a dummy payload, so the workload matches. This requires an `app.overlay` with the sensor nodes, which NFR-AW-04 currently rules out.
> - Publication interval: equal to the `main` firmware's `read_interval_s` for each interval in the experiment matrix, instead of 1 s.

---

## Purpose

The active WiFi firmware repeatedly connects to WiFi, publishes one MQTT message, disconnects, and immediately reconnects — mirroring the per-cycle behaviour of the main firmware. Its sole purpose is to provide a stable, measurable baseline WiFi-phase current (I_wifi) using a multimeter. This value feeds directly into the thesis energy model:

```
E_cycle = (I_sleep × t_sleep + I_boot × t_boot + I_sensor × t_sensor + I_wifi × t_wifi + I_ble × t_ble) × V
```

This firmware contains no real sensor reads and no sleep logic.

---

## Platform

| Item | Value |
|---|---|
| MCU | ESP32-C6 |
| Board | ESP32-C6-DevKitC-1 (`esp32c6_devkitc`) |
| RTOS | Zephyr RTOS v4.0 LTS |
| Language | C |

---

## Functional Requirements

### FR-AW-01: Tight Reconnect Loop
The firmware shall run a tight reconnect loop with no sleep between cycles:

1. Connect to WiFi (wait for association and DHCP)
2. Connect to MQTT broker
3. Publish one dummy `sensors/raw` payload
4. Disconnect MQTT
5. Disconnect WiFi
6. Immediately go to step 1

The device shall never enter any sleep or idle power state. There is no delay or sleep between cycles.

### FR-AW-02: MQTT Publishing Loop
Each loop iteration shall publish exactly one hardcoded dummy JSON payload to the topic `plants/active-test/sensors/raw`. The payload schema shall match the main firmware's `sensors/raw` schema exactly.

### FR-AW-03: Dummy Payload
The published payload shall use hardcoded, realistic-range values. No sensors are read.

```json
{
  "temperature_c": 23.4,
  "bmp280_temperature_c": 23.1,
  "humidity_pct": 61.2,
  "pressure_hpa": 1013.5,
  "lux": 450.0,
  "soil_moisture_pct": 65.0,
  "soil_moisture_raw_adc": 1820,
  "timestamp_ms": 1234567890
}
```

**Design decision:** Payload content (dummy vs real vs random values) has no impact on I_active. WiFi radio power is determined by RF transmission, not payload content. Dummy values were chosen for simplicity.

### FR-AW-04: Failure Handling
On any connection failure (WiFi association timeout, DHCP failure, MQTT connect rejection), the firmware shall immediately retry from step 1 of the reconnect loop. No backoff, no sleep, no halt.

### FR-AW-05: Hardcoded Credentials
WiFi SSID, WiFi password, MQTT broker address, and MQTT broker port shall be set at compile time via `prj.conf` or a local `credentials.conf` (gitignored). No runtime provisioning.

---

## Non-Functional Requirements

### NFR-AW-01: No Sleep States
All Zephyr power management shall be disabled. The MCU shall remain at full clock speed continuously.

### NFR-AW-02: Minimal Logging
Serial logging shall be kept at the minimum level necessary for debugging connection issues. Excessive logging can skew current draw measurements.

### NFR-AW-03: Single Thread Architecture
The firmware shall use a single Zephyr thread running a `while(1)` loop. No RTOS scheduling features are used beyond what Zephyr requires to boot.

### NFR-AW-04: Build System Integration
The firmware shall build within the shared west workspace at the repository root, targeting the upstream `esp32c6_devkitc` board. No `app.overlay` is required — no sensors or ADC are used.

---

## Kconfig

```
CONFIG_WIFI=y
CONFIG_BT=n
CONFIG_PM=n
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2
CONFIG_MQTT_LIB=y
```

---

## Measurement Procedure

This procedure shall be documented in the thesis methodology chapter. Revised 2026-10-15: it measures this firmware as comparison baseline A.

### Bench

- Board under test: MuseLab nanoESP32-C6, with the HDC1080, BMP280, BH1750FVI and the resistive soil moisture sensor (with its LM393 comparator module) connected and powered as in deployment.
- Power: a second board connected to the computer by USB acts as the 5 V source. The board under test is powered only through its 5 V pin, via the measurement path. All grounds are common.
- Neither USB-C port of the board under test may be connected during a measurement: a USB cable would power the board directly and bypass the measurement path. Connect USB only to flash, then disconnect it.
- Power source voltage: V = 5 V.

### Steps

1. Configure the publication interval for the condition under test, flash `apps/active-wifi` and disconnect USB.
2. Insert the INA219 in series in the 5 V line. Log at the same sampling settings used for the `main` trace.
3. Power the board and wait for the first publication on the broker.
4. Log at least 10 complete publication cycles per run; repeat three runs per condition.
5. Record the mean current over an integer number of cycles as **I_A**, and energy per cycle as I_A × V × interval.
6. From the same trace, record the mean current between publications as **I_wifi,idle** (Wi-Fi associated, no traffic). This term is needed to express baseline A in the energy model, which otherwise has no state for an associated but idle radio.
7. Sanity check: the reduction from baseline A to `main` with `ble_window_s` = 0 at a 60 s interval should fall near the 72–83% range reported by Mayo et al. (2026), Dzahir and Chia (2023) and Smyrilios et al. (2025). A result far outside that range points to a bench or firmware problem.
---

## Design Decisions

### DD-AW-01: Zephyr for All Firmwares
See DD-SS-01 in the Static Sleep Firmware PRD. The same rationale applies: I_active must include Zephyr's OS overhead to accurately model the main firmware's active-phase current.

### DD-AW-02: BLE Disabled
BLE is disabled in this firmware.

**Rationale:** The active measurement targets WiFi+MQTT current specifically. The main firmware does not run WiFi and BLE simultaneously (they are time-division multiplexed within the wake cycle). Including BLE in I_active would overestimate the WiFi-phase current.

### DD-AW-03: Tight Reconnect Loop Instead of Continuous Connection
The firmware uses a tight reconnect loop (connect → publish → disconnect → repeat) rather than a persistent connection with periodic publishes.

**Rationale:** A persistent connection measures steady-state WiFi TX current, which is lower than the average current during a real main-firmware WiFi phase. The main firmware's WiFi phase is dominated by connection establishment (association, DHCP, TCP, MQTT CONNECT) — a high-current transient that a persistent-connection measurement entirely misses. Using a tight reconnect loop causes the multimeter to average over the same operations the main firmware performs per cycle, including the transient overhead. This makes I_wifi directly representative of the main firmware's WiFi-phase current profile.

**Known approximation:** In the tight reconnect loop, the WiFi driver stack remains initialised between cycles (warm reconnect), whereas the main firmware re-initialises it from deep sleep (cold init). Cold init may draw slightly higher current than warm reconnect during driver startup. This difference is small relative to the association and DHCP transients and is acknowledged as a bounded systematic error in the methodology chapter.

### DD-AW-05: Payload Size Has Negligible Power Impact
JSON was chosen over compact binary formats (e.g., Protocol Buffers) for payload encoding.

**Rationale:** At WiFi transmission speeds, the difference between a ~120-byte JSON payload and a ~30-byte Protobuf payload corresponds to microseconds of additional transmission time — well below the noise floor of the energy model. The dominant cost in the active phase is WiFi connection establishment (DHCP, TCP, MQTT CONNECT), not payload transmission. JSON was therefore chosen for its simplicity across firmware, broker, and Android client.

**Thesis implication:** When discussing payload format in the paper, note that payload encoding was evaluated and found to have negligible impact on E_cycle for this sensor frequency and payload size.

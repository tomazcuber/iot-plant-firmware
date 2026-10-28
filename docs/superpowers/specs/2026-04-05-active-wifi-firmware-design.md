# PRD: Active WiFi Firmware

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Firmware:** `apps/active-wifi`  
**Date:** 2026-04-05  
**Status:** Approved

---

## Purpose

The active WiFi firmware repeatedly connects to WiFi, publishes one MQTT message, disconnects, and immediately reconnects — mirroring the per-cycle behaviour of the main firmware. Its sole purpose is to provide a stable, measurable baseline WiFi-phase current (I_wifi) using a multimeter. This value feeds directly into the thesis energy model:

```
E_cycle = (I_sleep × t_sleep + I_sensor × t_sensor + I_wifi × t_wifi + I_ble × t_ble) × V
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

This procedure shall be documented in the thesis methodology chapter.

1. Assemble the **complete main firmware hardware configuration** on the protoboard: ESP32-C6-DevKitC-1 with HDC1080, BMP280, BH1750FVI, and the resistive soil moisture sensor (with its onboard LM393 comparator module) all physically connected. Sensors are not read by this firmware but must be present so the measured current matches the main firmware's WiFi-phase hardware state.
2. Set WiFi credentials and MQTT broker address in `apps/active-wifi/credentials.conf` (gitignored; see FR-AW-05).
3. Flash `apps/active-wifi` to the ESP32-C6-DevKitC-1.
4. Connect a multimeter in series with the 5V USB VCC line.
5. Set multimeter to DC current mode (mA range).
6. Power the board. Allow 30 seconds for the reconnect loop to reach a steady cadence.
7. Record the stable current reading as **I_wifi**.
8. Power source voltage during measurement: **V = 5V** (USB).

> **Note:** The tight reconnect loop causes the current to oscillate between a high transient (during WiFi association and MQTT handshake) and a lower level (during publish and disconnect). The multimeter averages across these phases — this average is exactly the `I_wifi` the energy model requires. Do not treat the oscillating reading as instability; it is the correct operating state.

> **Measurement validity:** The reconnect loop should settle into a consistent cadence within 30 seconds. If the reading drifts continuously or the board appears stuck (no reconnect activity visible on the MQTT broker), investigate the network/broker and resolve before recording. The cadence will be faster on a strong network and slower on a marginal one — record I_wifi only at the intended deployment RF environment if possible.

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

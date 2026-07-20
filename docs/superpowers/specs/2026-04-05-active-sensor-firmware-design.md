# PRD: Active Sensor Firmware

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Firmware:** `apps/active-sensor`  
**Date:** 2026-04-05  
**Status:** Approved

---

## Purpose

The active sensor firmware continuously powers the I2C bus, reads all four sensors in sequence, powers the bus down, and immediately repeats. Its sole purpose is to provide a stable, measurable baseline sensor-phase current (I_sensor) using a multimeter. This value feeds directly into the thesis energy model:

```
E_cycle = (I_sleep × t_sleep + I_sensor × t_sensor + I_wifi × t_wifi + I_ble × t_ble) × V
```

This firmware contains no WiFi, no BLE, and no sleep logic.

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

### FR-AS-01: Continuous Sensor Read Loop
The firmware shall run a tight sensor read loop with no sleep between iterations:

1. Power on I2C bus
2. Read BMP280 (pressure + temperature)
3. Read BH1750FVI (lux)
4. Read HDC1080 (temperature + humidity)
5. Read soil moisture ADC
6. Power down I2C bus and ADC
7. Immediately go to step 1

The device shall never enter any sleep or idle power state. There is no delay between iterations.

### FR-AS-02: Sensor Read Order
The read order (BMP280 → BH1750FVI → HDC1080 → soil moisture ADC) shall match the main firmware's wake cycle sequence exactly (FR-MA-07, step 3). This ensures I_sensor reflects the same hardware activity pattern as the main firmware's sensor phase.

### FR-AS-03: WiFi and BLE Disabled
WiFi and BLE shall be fully disabled at the Kconfig level. No WiFi or BLE initialisation shall occur.

### FR-AS-04: I2C Bus Power Cycle per Iteration
The I2C bus shall be powered on at the start of each iteration and powered down at the end, matching the main firmware's per-cycle power management behaviour. This ensures I_sensor includes the bus enable/disable overhead present in the main firmware.

---

## Non-Functional Requirements

### NFR-AS-01: No Sleep States
All Zephyr power management shall be disabled. The MCU shall remain at full clock speed continuously.

### NFR-AS-02: Minimal Logging
Serial logging shall be kept at the minimum level necessary for confirming sensor reads are occurring. Excessive logging can skew current draw measurements.

### NFR-AS-03: Single Thread Architecture
The firmware shall use a single Zephyr thread running a `while(1)` loop. No RTOS scheduling features are used beyond what Zephyr requires to boot.

### NFR-AS-04: Build System Integration
The firmware shall build within the shared west workspace at the repository root, targeting the upstream `esp32c6_devkitc` board. Hardware-specific devicetree configuration (I2C sensor nodes, ADC channel for soil moisture) shall be provided via `apps/active-sensor/app.overlay`.

---

## Kconfig

```
CONFIG_WIFI=n
CONFIG_BT=n
CONFIG_PM=n
CONFIG_I2C=y
CONFIG_ADC=y
CONFIG_SENSOR=y
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2
```

---

## Measurement Procedure

This procedure shall be documented in the thesis methodology chapter.

1. Assemble the **complete main firmware hardware configuration** on the protoboard: ESP32-C6-DevKitC-1 with HDC1080, BMP280, BH1750FVI, and the resistive soil moisture sensor (with its onboard LM393 comparator module) all physically connected. All sensors must be present and wired as in the main firmware deployment.
2. Flash `apps/active-sensor` to the ESP32-C6-DevKitC-1.
3. Connect a multimeter in series with the 5V USB VCC line.
4. Set multimeter to DC current mode (mA range).
5. Power the board. Allow 10 seconds for the sensor read loop to reach a stable cadence.
6. Record the stable current reading as **I_sensor**.
7. Power source voltage during measurement: **V = 5V** (USB).

> **Note:** The continuous read loop will produce a steady average current with small periodic variation at the I2C transaction rate. The multimeter averages across many read cycles — this average is the I_sensor the energy model requires.

> **Measurement validity:** If any sensor is not responding (NACK, wiring fault), the loop will still run but the current profile will differ from the main firmware's sensor phase. Verify via serial log that all four sensors are returning valid reads before recording I_sensor. Fix any wiring issues and restart the measurement.

---

## Design Decisions

### DD-AS-01: Zephyr for All Firmwares
See DD-SS-01 in the Static Sleep Firmware PRD. I_sensor must include Zephyr's OS overhead to accurately model the main firmware's sensor phase.

### DD-AS-02: Read Order Matches Main Firmware
The sensor read order is fixed to match the main firmware exactly (FR-MA-07, step 3).

**Rationale:** If a sensor exhibits significant power-on transient current (e.g., BMP280 has a ~2ms startup time after power-on), the order in which sensors are read affects the current profile. Matching the order ensures I_sensor reflects the same transient sequence the main firmware experiences.

### DD-AS-03: I2C Bus Power Cycle per Iteration
The I2C bus is powered on and off each iteration rather than left continuously enabled.

**Rationale:** The main firmware powers the I2C bus on at the start of the sensor phase and powers it down before WiFi initialisation (FR-MA-07, steps 3–4). I_sensor must include the bus enable/disable overhead. Leaving the bus continuously enabled would underestimate I_sensor relative to the main firmware's behaviour.

# PRD: Active BLE Firmware

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Firmware:** `apps/active-ble`  
**Date:** 2026-04-05  
**Status:** Approved

---

## Purpose

The active BLE firmware keeps the ESP32-C6 continuously active with its BLE stack advertising. Its sole purpose is to provide a stable, measurable baseline BLE active current (I_ble) using a multimeter. This value feeds directly into the thesis energy model:

```
E_cycle = (I_sleep × t_sleep + I_sensor × t_sensor + I_wifi × t_wifi + I_ble × t_ble) × V
```

This firmware contains no sensor reads, no WiFi, and no sleep logic.

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

### FR-AB-01: Continuous BLE Advertising
The firmware shall continuously advertise as a BLE peripheral with no gaps. The device shall never enter any sleep or idle power state.

### FR-AB-02: Advertisement Parameters
The firmware shall advertise using the same service UUID as the main firmware's GATT streaming service. The advertising interval shall match the main firmware's BLE advertising configuration. The device name in the advertisement shall be `PlantMonitor-BLE-TEST`.

### FR-AB-03: No Connections Accepted
The firmware shall advertise as non-connectable (undirected advertising, non-connectable mode). No GATT server is required. This ensures the radio operates in a stable, repeatable advertising-only state with no connection-phase current transients.

### FR-AB-04: WiFi Disabled
WiFi shall be fully disabled at the Kconfig level. No WiFi initialisation shall occur.

---

## Non-Functional Requirements

### NFR-AB-01: No Sleep States
All Zephyr power management shall be disabled. The MCU shall remain at full clock speed continuously.

### NFR-AB-02: Minimal Logging
Serial logging shall be kept at the minimum level necessary for debugging. Excessive logging can skew current draw measurements.

### NFR-AB-03: Single Thread Architecture
The firmware shall use a single Zephyr thread running a `while(1)` loop. No RTOS scheduling features are used beyond what Zephyr requires to boot.

### NFR-AB-04: Build System Integration
The firmware shall build within the shared west workspace at the repository root, targeting the upstream `esp32c6_devkitc` board. No `app.overlay` is required — no sensors or ADC are used.

---

## Kconfig

```
CONFIG_WIFI=n
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="PlantMonitor-BLE-TEST"
CONFIG_PM=n
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=2
```

---

## Measurement Procedure

This procedure shall be documented in the thesis methodology chapter.

1. Assemble the **complete main firmware hardware configuration** on the protoboard: ESP32-C6-DevKitC-1 with HDC1080, BMP280, BH1750FVI, and the resistive soil moisture sensor (with its onboard LM393 comparator module) all physically connected. Sensors are not read by this firmware but must be present so the measured current matches the main firmware's BLE-phase hardware state.
2. Flash `apps/active-ble` to the ESP32-C6-DevKitC-1.
3. Connect a multimeter in series with the 5V USB VCC line.
4. Set multimeter to DC current mode (mA range).
5. Power the board. Allow 5 seconds for the firmware to boot and begin advertising.
6. Record the stable current reading as **I_ble**.
7. Power source voltage during measurement: **V = 5V** (USB).

> **Note:** BLE advertising current is significantly lower than WiFi TX current and will show small periodic spikes at the advertising interval. Record the average stable value. A multimeter with averaging capability is preferred.

---

## Design Decisions

### DD-AB-01: Zephyr for All Firmwares
See DD-SS-01 in the Static Sleep Firmware PRD. I_ble must include Zephyr's OS overhead to accurately model the main firmware's BLE advertising phase.

### DD-AB-02: Non-Connectable Advertising
The firmware advertises as non-connectable rather than connectable.

**Rationale:** The main firmware's BLE advertising window is bounded and may or may not result in a client connection. Measuring I_ble during connectable advertising with no client present produces the same radio state as the main firmware during the window when no Android client connects — which is the common case. Non-connectable mode eliminates the risk of a test device inadvertently connecting and altering the current profile during measurement.

### DD-AB-03: WiFi Disabled
WiFi is disabled at the Kconfig level, not just uninitialised.

**Rationale:** Mirrors the main firmware's BLE phase, in which WiFi has been fully torn down before BLE advertising begins (DD-MA-09). Measuring I_ble with WiFi initialised but idle would include WiFi stack quiescent current not present in the main firmware's BLE phase.

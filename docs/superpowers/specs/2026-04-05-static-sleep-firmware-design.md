# PRD: Static Sleep Firmware

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Firmware:** `apps/static-sleep`  
**Date:** 2026-04-05  
**Status:** Approved

---

## Purpose

The static sleep firmware keeps the ESP32-C6 in permanent deep sleep. Its sole purpose is to provide a stable, measurable baseline sleep current (I_sleep) using a multimeter. This value feeds directly into the thesis energy model:

```
E_cycle = (I_sleep × t_sleep + I_sensor × t_sensor + I_wifi × t_wifi + I_ble × t_ble) × V
```

I_wifi and I_ble are measured by the `active-wifi` and `active-ble` baseline firmwares respectively.

This firmware contains no application logic.

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

### FR-SS-01: Immediate Deep Sleep
On boot, the firmware shall call Zephyr's power management API to enter deep sleep with no scheduled wakeup timer. The device shall remain in deep sleep until an external reset (EN button or power cycle).

### FR-SS-02: No Peripheral Initialization
No peripherals shall be initialized prior to entering deep sleep — no WiFi, no BLE, no I2C, no ADC, no UART beyond the minimum required to execute the PM call. All GPIOs shall be left in high-impedance state to minimise leakage current.

### FR-SS-03: Minimal Kernel Footprint
No application threads shall be created beyond the minimal Zephyr kernel required to reach the PM call. No logging, no shell, no console.

---

## Non-Functional Requirements

### NFR-SS-01: Reproducibility
The firmware shall produce a stable, reproducible I_sleep measurement across resets. Any variance in the reading is attributable to the measurement instrument, not firmware behaviour.

### NFR-SS-02: Build System Integration
The firmware shall build within the shared west workspace at the repository root, targeting the upstream `esp32c6_devkitc` board. No `app.overlay` is required — no peripherals are used.

---

## Kconfig

```
CONFIG_WIFI=n
CONFIG_BT=n
CONFIG_LOG=n
CONFIG_CONSOLE=n
CONFIG_UART_CONSOLE=n
CONFIG_PM=y
CONFIG_PM_DEVICE=y
```

---

## Measurement Procedure

This procedure shall be documented in the thesis methodology chapter.

1. Assemble the **complete main firmware hardware configuration** on the protoboard: ESP32-C6-DevKitC-1 with HDC1080, BMP280, BH1750FVI, and the resistive soil moisture sensor (with its onboard LM393 comparator module) all physically connected and powered as they would be in a real deployment.
2. Flash `apps/static-sleep` to the ESP32-C6-DevKitC-1.
3. Connect a multimeter in series with the 5V USB VCC line (between USB power source and board VBUS pin).
4. Set multimeter to DC current mode (mA range).
5. Power the board. Allow 5 seconds for the firmware to boot and enter deep sleep.
6. Record the stable current reading as **I_sleep**.
7. Power source voltage during measurement: **V = 5V** (USB).

> **Note:** The DevKitC-1 has an onboard LDO regulator. The measured I_sleep includes the LDO's quiescent current (~50–100 µA), the I2C sensors' idle current (~1–10 µA each), and the LM393 comparator module's continuous draw (~1–2 mA). This is intentional — I_sleep represents the true system-level sleep current of the deployed configuration, not the bare MCU current. The dominant contributor is typically the LM393 module; this should be noted explicitly in the thesis methodology.

---

## Design Decisions

### DD-SS-01: Zephyr for All Firmwares
All three measurement firmwares are built on Zephyr v4.0 LTS rather than bare-metal ESP-IDF.

**Rationale:** The main application firmware runs on Zephyr. Using Zephyr across all three firmwares ensures that I_sleep and I_active include the same OS-level overhead as the main firmware, making the energy model accurate. A bare-metal measurement would underestimate real sleep/active current by excluding OS overhead.

**Thesis implication:** When citing I_sleep in the paper, explicitly note that it represents system-level sleep current under Zephyr, not bare MCU sleep current.

### DD-SS-02: No Wakeup Timer
The device sleeps indefinitely (no RTC wakeup timer configured).

**Rationale:** A periodic wakeup would introduce brief active spikes into the I_sleep measurement, corrupting the baseline. The measurement requires the device to remain continuously in deep sleep.

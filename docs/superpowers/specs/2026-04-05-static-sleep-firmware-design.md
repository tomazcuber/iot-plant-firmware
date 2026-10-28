# PRD: Static Sleep Firmware

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Firmware:** `apps/static-sleep`  
**Date:** 2026-04-05  
**Status:** Approved (measurement procedure revised 2026-10-15 for the INA219 bench)

---

## Purpose

The static sleep firmware keeps the ESP32-C6 in permanent deep sleep. Its sole purpose is to provide a stable, measurable baseline sleep current (I_sleep) using a multimeter. This value feeds directly into the thesis energy model:

```
E_cycle = (I_sleep × t_sleep + I_boot × t_boot + I_sensor × t_sensor + I_wifi × t_wifi + I_ble × t_ble) × V
```

I_boot, I_sensor, I_wifi and I_ble, and the phase durations, are obtained from the INA219 current trace of the `main` firmware (see the main firmware PRD, Measurement Procedure). I_sleep remains a separate measurement because the INA219 module (0.1 Ω shunt, about 0.1 mA resolution) cannot resolve the sleep floor accurately.

This firmware contains no application logic.

---

## Platform

| Item | Value |
|---|---|
| MCU | ESP32-C6 |
| Board | ESP32-C6, build target `esp32c6_devkitc/esp32c6/hpcore`; measured on the MuseLab nanoESP32-C6 bench board (see Measurement Procedure) |
| RTOS | Zephyr RTOS v4.3.1 (pinned in `west.yml`; see DD-SS-04 for why v4.2.2 does not work) |
| Language | C |

---

## Functional Requirements

### FR-SS-01: Immediate Deep Sleep
On boot, the firmware shall call Zephyr's system power-off API, `sys_poweroff()` (`<zephyr/sys/poweroff.h>`), which enters ESP32-C6 deep sleep, with no wakeup source configured and no delay or other work beforehand. The device shall remain in deep sleep until an external reset (EN button or power cycle).

### FR-SS-02: No Peripheral Initialization
No peripherals shall be initialized prior to entering deep sleep — no WiFi, no BLE, no I2C, no ADC, no UART beyond the minimum required to execute the power-off call. All GPIOs shall be left in high-impedance state to minimise leakage current.

### FR-SS-03: Minimal Kernel Footprint
No application threads shall be created beyond the minimal Zephyr kernel required to reach the power-off call. No logging, no shell, no console.

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
CONFIG_POWEROFF=y
```

`CONFIG_PM` and `CONFIG_PM_DEVICE` shall stay disabled (see DD-SS-03).

---

## Measurement Procedure

This procedure shall be documented in the thesis methodology chapter. Revised 2026-10-15: the active phases are now measured with an INA219; the sleep current of this firmware is still measured primarily with the multimeter.

### Bench

- Board under test: MuseLab nanoESP32-C6, with the HDC1080, BMP280, BH1750FVI and the resistive soil moisture sensor (with its LM393 comparator module) connected and powered as in deployment.
- Power: a second board connected to the computer by USB acts as the 5 V source. The board under test is powered only through its 5 V pin, via the measurement path. All grounds are common.
- Neither USB-C port of the board under test may be connected during a measurement: a USB cable would power the board directly and bypass the measurement path. Connect USB only to flash, then disconnect it.
- Power source voltage: V = 5 V.

### Steps

1. Flash `apps/static-sleep` and disconnect USB.
2. Insert the multimeter in series in the 5 V line (DC current, mA range), with a jumper wire shorting the multimeter terminals.
3. Power the board. Wait 5 seconds for the firmware to boot and enter deep sleep, then remove the jumper. The board draws tens of mA while booting, and the multimeter's burden voltage on low ranges can brown it out; the jumper carries the boot current.
4. Record the stable reading as **I_sleep**. Repeat three times, power-cycling between runs, and report the mean.
5. Cross-check: put the INA219 in the same position and log for 60 s. Its mean shall agree with the multimeter within the INA219 resolution.
6. Variant without the comparator: repeat steps 3 and 4 with the LM393 module's VCC disconnected and record **I_sleep,noLM393**. The difference quantifies the standby cost of event-driven wake (DD-MA-01 in the main firmware PRD): the LM393 output is the GPIO wake source, so the module must stay powered in deployment.

> **Note:** I_sleep is the system-level floor of the nanoESP32-C6 configuration. It includes the board's regulator, its USB-to-serial converter (CH343) and RGB LED quiescent current, the I2C sensors' idle current and the LM393 module's continuous draw (about 1–2 mA, typically the dominant term). The nanoESP32-C6 has no jumper to isolate the module current, so the bare-MCU sleep current is not measured; when the thesis cites it, it shall come from the Espressif datasheet and be labelled as such. Confirm that the firmware leaves the RGB LED off.
---

## Design Decisions

### DD-SS-01: Zephyr for All Firmwares
All three measurement firmwares are built on Zephyr rather than bare-metal ESP-IDF.

**Rationale:** The main application firmware runs on Zephyr. Using Zephyr across all three firmwares ensures that I_sleep and I_active include the same OS-level overhead as the main firmware, making the energy model accurate. A bare-metal measurement would underestimate real sleep/active current by excluding OS overhead.

**Thesis implication:** When citing I_sleep in the paper, explicitly note that it represents system-level sleep current under Zephyr, not bare MCU sleep current.

### DD-SS-02: No Wakeup Timer
The device sleeps indefinitely (no RTC wakeup timer configured).

**Rationale:** A periodic wakeup would introduce brief active spikes into the I_sleep measurement, corrupting the baseline. The measurement requires the device to remain continuously in deep sleep.

### DD-SS-03: `sys_poweroff()`, not the PM subsystem
Deep sleep is entered with `sys_poweroff()` and `CONFIG_PM` is disabled. Two problems rule out the PM subsystem on the ESP32-C6:
- With `CONFIG_PM=y` the idle policy selects STANDBY during `k_sleep`, and the SoC hook calls `esp_light_sleep_start()` with no wake source configured. The device resets about every 2 s with `rst:0x10 (LP_WDT_SYS)`, which looks like an RTC watchdog reset but is not one: the RWDT is already disabled when `main()` starts.
- The SoC's `pm_state_set()` handles only `PM_STATE_STANDBY`; `pm_state_force(PM_STATE_SOFT_OFF)` does nothing, so the chip never enters deep sleep.

`sys_poweroff()` is the path used by `samples/boards/espressif/deep_sleep`.

### DD-SS-04: Zephyr v4.3.1 or later
On Zephyr v4.2.2 `sys_poweroff()` aborts on ESP32-C6 chip revision v0.2: the fast RC clock calibration returns 0 and `assert(period)` fails in `rtc_time_us_to_slowclk()` (zephyrproject-rtos/zephyr#94238). The fix is hal_espressif commit `80f3bd427f` ("esp32c6: fix RTC calibration for ECO1 rev"), included from Zephyr v4.3.1, which still builds with Zephyr SDK 0.17.4.

**Verification note:** with the console disabled the firmware is silent in deep sleep, so a quiet serial port does not by itself prove deep sleep. The abort is printed before the chip halts, so check that no `abort()` appears after reset, and confirm deep sleep from the I_sleep reading (it must be far below the awake current).

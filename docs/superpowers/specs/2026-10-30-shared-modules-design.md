# Design: Shared Firmware Modules

**Project:** IoT Plant Monitoring — Bachelor's Thesis  
**Scope:** code and build configuration shared between `apps/main` and `apps/active-wifi` (comparison baseline A)  
**Date:** 2026-10-30  
**Status:** Approved, not implemented  
**Zephyr:** v4.3.1 (all facts below were checked against this tree)  
**Decisions:** [ADR-0005](../../adr/0005-shared-modules-zephyr-module-shield-snippet.md) (packaging), [ADR-0006](../../adr/0006-comparison-baseline-same-stack.md) (baseline A uses the same stack)

> This spec supersedes the "Shared / Scaffolding" section of the 2026-04-26 implementation plan (custom board definition and a `drivers/soil_moisture` west module). Neither is built.

---

## Purpose

Two firmwares do the same per-publish work: `apps/main` and comparison baseline A (`apps/active-wifi`). Both read the same sensors in the same order, encode the same JSON payloads, and publish them over the same WiFi and MQTT stack. They differ only in *architecture*: `main` runs deep-sleep cycles and connects every cycle; baseline A stays associated and never sleeps. The thesis compares their energy, so every difference that is not architecture is a confound.

This spec defines the code and configuration the two apps share, so that the shared part is identical by construction and only the orchestration differs.

---

## Consumers

| Shared piece | `main` | `active-wifi` (baseline A) | `active-ble` | `static-sleep` |
|---|---|---|---|---|
| `plant_sensors` shield (devicetree + sensor Kconfig + calibration) | ✓ | ✓ | – | – |
| `plant_sensors` module (sensor-phase read, soil helper) | ✓ | ✓ | – | – |
| `plant_json` module (payload encoding) | ✓ | ✓ | – | – |
| `plant-net` snippet (network Kconfig) | ✓ | ✓ | – | – |
| `plant_net` module (WiFi, MQTT, SNTP, RSSI primitives) | ✓ | ✓ | – | – |

`active-ble` and `static-sleep` use none of it. Their builds are unchanged: the module is discovered but compiles nothing while its Kconfig symbols are off.

These stay private to `apps/main`, because they have one user:
- **Phase-marker GPIOs** (proposed FR-MA-18). Written as a C unit with a C header inside `apps/main/src/` (`phase_markers.{h,c}`), so a later move to `lib/` is a file move plus one Kconfig/CMake line.
- **BLE dynamic name** (`PlantMonitor-XXXX`), provisioning, BLE streaming, Settings, retained RAM, the cycle state machine.

---

## Repository layout

```
iot-plant-firmware/
├── zephyr/
│   └── module.yml                      # SM-01
├── CMakeLists.txt                      # module entry point (not an app)
├── Kconfig                             # module Kconfig entry point
├── include/
│   └── plant/
│       └── types.h                     # shared value types, Zephyr-free (SM-20)
├── lib/
│   ├── plant_sensors/
│   │   ├── CMakeLists.txt
│   │   ├── Kconfig
│   │   ├── include/plant/sensors.h
│   │   ├── sensors.c                   # read sequence (Zephyr)
│   │   ├── soil.c                      # soil ADC read (Zephyr)
│   │   └── soil_calib.c                # plant_soil_pct(), Zephyr-free
│   ├── plant_json/
│   │   ├── CMakeLists.txt
│   │   ├── Kconfig
│   │   ├── include/plant/json.h
│   │   └── json.c                      # Zephyr-free
│   └── plant_net/
│       ├── CMakeLists.txt
│       ├── Kconfig
│       ├── include/plant/net.h
│       ├── wifi.c
│       ├── mqtt.c
│       └── sntp.c
├── boards/
│   └── shields/
│       └── plant_sensors/
│           ├── shield.yml
│           ├── Kconfig.shield
│           ├── plant_sensors.overlay
│           └── plant_sensors.conf
├── snippets/
│   └── plant-net/
│       ├── snippet.yml
│       └── plant-net.conf
├── tests/
│   └── host/
│       ├── Makefile
│       ├── test_json.c
│       └── test_soil_calib.c
└── apps/ …
```

---

## Requirements

### Zephyr module

#### SM-01: The repository is a Zephyr module
The repository root shall contain `zephyr/module.yml`:

```yaml
name: plant
build:
  cmake: .
  kconfig: Kconfig
  settings:
    board_root: .
    dts_root: .
    snippet_root: .
```

The repository is the west manifest repository (`self`). Zephyr's module discovery includes the manifest project, so the module is found in every build in this workspace without extra arguments. A build outside the west workspace needs `-DZEPHYR_EXTRA_MODULES=<repo>`.

#### SM-02: Root build entry points
```cmake
# CMakeLists.txt (root)
zephyr_include_directories(include)
add_subdirectory_ifdef(CONFIG_PLANT_SENSORS lib/plant_sensors)
add_subdirectory_ifdef(CONFIG_PLANT_JSON    lib/plant_json)
add_subdirectory_ifdef(CONFIG_PLANT_NET     lib/plant_net)
```

```kconfig
# Kconfig (root)
rsource "lib/plant_sensors/Kconfig"
rsource "lib/plant_json/Kconfig"
rsource "lib/plant_net/Kconfig"
```

Each module's `CMakeLists.txt` uses `zephyr_library()`, `zephyr_library_sources(...)` and `zephyr_include_directories(include)`.

#### SM-03: Modules are off by default
`PLANT_SENSORS`, `PLANT_JSON` and `PLANT_NET` default to `n`. An app that enables none of them builds exactly as it would without the module.

### Opt-in contract

#### SM-04: Apps opt in with a shield and a snippet
A consuming app opts in with two lines in its `CMakeLists.txt`, before `find_package(Zephyr …)`:

```cmake
set(SHIELD plant_sensors)
set(SNIPPET plant-net)
```

They are set in CMake rather than passed on the command line, so a plain `west build` reproduces the measured configuration.

#### SM-05: Apps never set shared symbols
Apps shall not set any `CONFIG_PLANT_*` symbol, or any Zephyr symbol listed in SM-12 or SM-14, in their own `prj.conf`. The shield and the snippet are the only places where these values are set.

This rule is enforced by merge order. Zephyr merges board defconfig → app `prj.conf` → shield `.conf` → `EXTRA_CONF_FILE` (which is where snippets append). A value set by the shield or the snippet therefore overrides `prj.conf` silently. An app can only override deliberately, through its own `EXTRA_CONF_FILE`, and doing so for a measured build breaks [ADR-0006](../../adr/0006-comparison-baseline-same-stack.md).

#### SM-06: App-specific devicetree stays in the app
Devicetree that only one app needs stays in that app's `app.overlay`. In particular, the LM393 comparator output on **GPIO6** (deep-sleep wake source) is configured only in `apps/main/app.overlay`.

### `plant_sensors` shield

#### SM-07: Shield files
`boards/shields/plant_sensors/` contains `shield.yml`, `Kconfig.shield`, `plant_sensors.overlay` and `plant_sensors.conf`.

```yaml
# shield.yml
shield:
  name: plant_sensors
  full_name: Plant monitor sensor set
  vendor: tomazcuber
```

```kconfig
# Kconfig.shield
config SHIELD_PLANT_SENSORS
	def_bool $(shields_list_contains,plant_sensors)
```

#### SM-08: I2C bus on GPIO4/GPIO5
The upstream `esp32c6_devkitc` board routes I2C0 to **SDA = GPIO6, SCL = GPIO7**. GPIO6 is the LM393 wake pin. The shield overlay shall define its own pinctrl state with SDA on **GPIO4** and SCL on **GPIO5** (pull-ups, open drain) and assign it to `&i2c0`, so the board default is never applied. The overlay shall carry a comment saying that this move frees GPIO6, which `apps/main/app.overlay` configures as the wake source (SM-06).

The bus clock stays at the board default.

#### SM-09: Sensor nodes
On `&i2c0` (`status = "okay"`):

| Node label | Compatible | Address | Note |
|---|---|---|---|
| `bmp280` | `bosch,bme280` | `0x76` or `0x77` | There is no `bosch,bmp280` binding. The BME280 driver detects the BMP280 by chip ID. The address depends on the module's SDO strap and shall be confirmed against the wiring. |
| `bh1750` | `rohm,bh1750` | `0x23` | ADDR pin low. |
| `hdc1080` | `ti,hdc` | `0x40` | Fixed address. The HDC1080 reports device ID `0x1050`, which the driver accepts. No `drdy-gpios`: the HDC1080 has no DRDY pin, and the driver falls back to a fixed conversion delay. |

#### SM-10: Soil moisture ADC channel
`&adc0` (`status = "okay"`) gets `channel@2` (GPIO2 = ADC1_CH2) with:
- `zephyr,gain = "ADC_GAIN_1_4"` (maps to 12 dB attenuation in the ESP32 ADC driver, full-scale range for a 3.3 V probe output);
- `zephyr,reference = "ADC_REF_INTERNAL"`;
- `zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>`;
- `zephyr,resolution = <12>`.

The channel is exposed to the soil helper through `/ { zephyr,user { io-channels = <&adc0 2>; }; };`. Other overlays may add properties to `zephyr,user`; the merge keeps both.

#### SM-11: Sensors put themselves to sleep
No code powers the I2C bus or ADC down between reads. The ESP32 I2C and ADC drivers have no device power-management support, and deep sleep powers down the high-performance domain anyway. Instead, every sensor shall return to its own low-power state after each measurement:

| Sensor | Mechanism |
|---|---|
| BMP280 | `CONFIG_BME280_MODE_FORCED=y` (set by the shield, SM-12). The driver's default is *normal* mode, which keeps measuring on its own timer, including through deep sleep. |
| BH1750 | The driver uses one-time measurement modes, which power down after each measurement. |
| HDC1080 | Performs one conversion per request, then returns to sleep. |

Forced mode also makes the BMP280's state during `main`'s deep sleep match its power-on state in `static-sleep`, so `I_sleep` measured on `static-sleep` applies to `main`.

#### SM-12: Shield Kconfig fragment
`plant_sensors.conf` sets at least:

```
CONFIG_I2C=y
CONFIG_ADC=y
CONFIG_SENSOR=y
CONFIG_BME280_MODE_FORCED=y
CONFIG_PLANT_SENSORS=y
# Two-point soil calibration (DD-MA-12). Replace with values characterised
# for the deployed probe; record them in the thesis methodology.
CONFIG_PLANT_SOIL_DRY_ADC=3000
CONFIG_PLANT_SOIL_WET_ADC=1500
```

The shield's `.conf` is the single place where the probe's characterised calibration is set, because calibration describes the physical probe, which is what the shield represents.

### `plant-net` snippet

#### SM-13: Snippet files
```yaml
# snippets/plant-net/snippet.yml
name: plant-net
append:
  EXTRA_CONF_FILE: plant-net.conf
```

#### SM-14: Snippet Kconfig fragment
`plant-net.conf` sets at least the following. The exact list of networking symbols is completed at implementation time. Every symbol that affects radio or protocol behaviour belongs here, not in an app's `prj.conf`.

```
CONFIG_PLANT_JSON=y
CONFIG_PLANT_NET=y

# Float formatting for snprintf (plant_json); picolibc is the default libc
CONFIG_PICOLIBC_IO_FLOAT=y

# Network stack
CONFIG_NETWORKING=y
CONFIG_WIFI=y
CONFIG_NET_IPV4=y
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_DHCPV4=y
CONFIG_DNS_RESOLVER=y
CONFIG_NET_MGMT=y
CONFIG_NET_MGMT_EVENT=y
CONFIG_MQTT_LIB=y
CONFIG_SNTP=y

# Shared stack settings (ADR-0006)
CONFIG_PLANT_NET_WIFI_PS_MIN_MODEM=y
CONFIG_MQTT_KEEPALIVE=60
CONFIG_PLANT_NET_MQTT_PORT=1883
CONFIG_PLANT_NET_SNTP_SERVER="pool.ntp.org"
```

#### SM-15: Addressing is DHCP
Both apps obtain their IPv4 address over DHCP. `main` loses its lease in deep sleep and runs DHCP every cycle; that cost is part of `t_wifi`. Static IP would be a separate, measured change with its own ADR. Under ADR-0006 it would apply to both apps.

#### SM-16: WiFi power save is set explicitly
The Zephyr ESP32 WiFi driver does not implement Zephyr's WiFi power-save management call. Without intervention, the Espressif library's default (`WIFI_PS_MIN_MODEM`) applies silently. `plant_net` shall therefore call `esp_wifi_set_ps()` after every successful association, with the mode selected by a Kconfig choice:

```kconfig
choice PLANT_NET_WIFI_PS
	prompt "WiFi station power-save mode"
	default PLANT_NET_WIFI_PS_MIN_MODEM
config PLANT_NET_WIFI_PS_NONE
	bool "None (receiver always on)"
config PLANT_NET_WIFI_PS_MIN_MODEM
	bool "Minimum modem sleep (wake every DTIM)"
config PLANT_NET_WIFI_PS_MAX_MODEM
	bool "Maximum modem sleep (wake every listen interval)"
endchoice
```

The selected mode is `MIN_MODEM` ([ADR-0006](../../adr/0006-comparison-baseline-same-stack.md)). The thesis records it together with the access point's DTIM period.

### Shared value types

#### SM-20: `include/plant/types.h` is Zephyr-free
Value types used by more than one module live in `include/plant/types.h`. The root `CMakeLists.txt` puts this directory on every build's include path (SM-02). The header uses only `<stdint.h>` and `<stdbool.h>`, so the host tests can include it.

```c
/* include/plant/types.h */
#ifndef PLANT_TYPES_H
#define PLANT_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Validity bits for struct plant_sensor_values.valid */
#define PLANT_VALID_TEMPERATURE_C          (1u << 0)  /* HDC1080 */
#define PLANT_VALID_HUMIDITY_PCT           (1u << 1)  /* HDC1080 */
#define PLANT_VALID_BMP280_TEMPERATURE_C   (1u << 2)  /* BMP280 */
#define PLANT_VALID_PRESSURE_HPA           (1u << 3)  /* BMP280 */
#define PLANT_VALID_LUX                    (1u << 4)  /* BH1750FVI */
#define PLANT_VALID_SOIL_MOISTURE_PCT      (1u << 5)  /* soil probe */
#define PLANT_VALID_SOIL_MOISTURE_RAW_ADC  (1u << 6)  /* soil probe */

/* One sensor phase. A field is meaningful only when its bit is set in `valid`. */
struct plant_sensor_values {
	float temperature_c;
	float humidity_pct;
	float bmp280_temperature_c;
	float pressure_hpa;
	float lux;
	float soil_moisture_pct;
	uint16_t soil_moisture_raw_adc;
	uint8_t valid;
};

enum plant_wake_reason {
	PLANT_WAKE_TIMER,                /* "timer" */
	PLANT_WAKE_SOIL_MOISTURE_ALERT,  /* "soil_moisture_alert" */
};

/* Presence bits for struct plant_status.present */
#define PLANT_STATUS_WAKE_REASON    (1u << 0)
#define PLANT_STATUS_T_ACTIVE_MS    (1u << 1)
#define PLANT_STATUS_T_SENSOR_MS    (1u << 2)
#define PLANT_STATUS_T_WIFI_MS      (1u << 3)
#define PLANT_STATUS_T_BLE_MS       (1u << 4)
#define PLANT_STATUS_T_SLEEP_MS     (1u << 5)
#define PLANT_STATUS_WIFI_RSSI_DBM  (1u << 6)
#define PLANT_STATUS_CYCLE_COUNT    (1u << 7)
#define PLANT_STATUS_TIMESTAMP_MS   (1u << 8)

/* One `status` payload. A field absent from `present` is encoded as JSON null. */
struct plant_status {
	enum plant_wake_reason wake_reason;
	uint32_t t_active_ms;
	uint32_t t_sensor_ms;
	uint32_t t_wifi_ms;
	uint32_t t_ble_ms;
	uint32_t t_sleep_ms;
	int16_t wifi_rssi_dbm;
	uint32_t cycle_count;
	int64_t timestamp_ms;
	uint16_t present;
};

#ifdef __cplusplus
}
#endif
#endif
```

A failure that affects a whole sensor clears every bit belonging to it. A BMP280 failure clears both BMP280 bits; a failed ADC read clears both soil bits.

### `plant_sensors` module

#### SM-21: API
```c
/* lib/plant_sensors/include/plant/sensors.h */
#include <plant/types.h>

/* Check that all sensor devices and the ADC channel are ready, and configure
 * the ADC channel. Call once per boot, before the first read.
 * Returns 0, or -ENODEV if any device is not ready. A not-ready device does
 * not stop the app: its fields are simply never valid. */
int plant_sensors_init(void);

/* Run the sensor phase: BMP280 → BH1750FVI → HDC1080 → soil moisture ADC.
 * Each sensor is attempted exactly once; no retries (FR-MA-08a).
 * Fills *out and sets a `valid` bit for each field that was read.
 * Returns the number of sensors that failed (0..4). Never returns early. */
int plant_sensors_read(struct plant_sensor_values *out);

/* Pure two-point calibration (DD-MA-12), no Zephyr dependency.
 * pct = 100 × (dry − raw) / (dry − wet), clamped to [0, 100].
 * Precondition: dry != wet (enforced at build time for the Kconfig values). */
float plant_soil_pct(uint16_t raw, uint16_t dry_adc, uint16_t wet_adc);
```

#### SM-22: Read order and failure handling
`plant_sensors_read()` shall follow the order in FR-MA-07 step 3, so both apps produce the same I2C/ADC activity. A failure (driver error, NACK, timeout) leaves that sensor's bits cleared and moves on to the next sensor.

#### SM-23: Soil helper
`soil.c` reads the `zephyr,user` `io-channels` entry with `ADC_DT_SPEC_GET(DT_PATH(zephyr_user))`, `adc_sequence_init_dt()` and `adc_read_dt()`, then calls `plant_soil_pct()` with `CONFIG_PLANT_SOIL_DRY_ADC` / `CONFIG_PLANT_SOIL_WET_ADC`. `soil_calib.c` contains only `plant_soil_pct()` and includes no Zephyr header.

The percentage is clamped to [0, 100]. The raw value is always reported unclamped, so out-of-calibration readings stay visible in the dataset and can be recalibrated later.

A `BUILD_ASSERT(CONFIG_PLANT_SOIL_DRY_ADC != CONFIG_PLANT_SOIL_WET_ADC)` in `soil.c` rejects a calibration that would divide by zero.

#### SM-24: Kconfig
```kconfig
config PLANT_SENSORS
	bool "Plant sensor-phase read"
	depends on I2C && ADC && SENSOR

if PLANT_SENSORS
config PLANT_SOIL_DRY_ADC
	int "Raw ADC value, probe in dry air"
	range 0 4095
	default 3000
config PLANT_SOIL_WET_ADC
	int "Raw ADC value, probe submerged"
	range 0 4095
	default 1500
endif
```

The defaults are placeholders. Measured values are set in the shield's `.conf` (SM-12).

### `plant_json` module

#### SM-30: API
```c
/* lib/plant_json/include/plant/json.h */
#include <stddef.h>
#include <stdint.h>
#include <plant/types.h>

#define PLANT_JSON_RAW_MAX     256
#define PLANT_JSON_STATUS_MAX  256

/* Encode the sensors/raw payload (FR-MA-10). timestamp_ms may be NULL → null.
 * Returns the length written (without the NUL), or -ENOMEM if buf is too small. */
int plant_json_encode_raw(const struct plant_sensor_values *v,
			  const int64_t *timestamp_ms,
			  char *buf, size_t len);

/* Encode the status payload (FR-MA-10).
 * Returns the length written (without the NUL), or -ENOMEM. */
int plant_json_encode_status(const struct plant_status *s,
			     char *buf, size_t len);
```

The same `plant_json_encode_raw()` output is used for `main`'s BLE `sensor_data` notification (FR-MA-13).

#### SM-31: Encoding rules
These rules make the output byte-identical across apps and across host and target builds:
- **Field order:** exactly as in FR-MA-10.
- **Compact output:** no whitespace.
- **Floats:** one decimal place (`%.1f`).
- **Integers:** decimal.
- **Wake reason:** the wire strings from [ADR-0002](../../adr/0002-wake-reason-wire-string.md) (`"timer"`, `"soil_moisture_alert"`).
- **Invalid or absent fields:** encoded as the literal `null`. NaN or infinity is never emitted, because a field is either valid or `null`.

#### SM-32: Zephyr-free
`json.c` uses only the C standard library (`snprintf`, `<errno.h>`, `<stdint.h>`, `<stdbool.h>`) and `<plant/types.h>`. It compiles unchanged with a host `gcc` (SM-50).

#### SM-33: Kconfig
`config PLANT_JSON` (bool, default `n`). It has no dependency on `PLANT_NET` or `PLANT_SENSORS`: it only encodes structs defined in `include/plant/types.h`.

### `plant_net` module

#### SM-40: API
```c
/* lib/plant_net/include/plant/net.h */
#include <stddef.h>
#include <stdint.h>

/* Register network-management callbacks and initialise the MQTT client.
 * Call once per boot. */
int plant_net_init(void);

/* Associate, wait for a DHCP lease, then apply the Kconfig power-save mode
 * (SM-16). Blocks for at most timeout_ms. */
int plant_wifi_connect(const char *ssid, const char *psk, int32_t timeout_ms);

/* Disassociate and bring the interface down. */
int plant_wifi_disconnect(void);

/* Current RSSI of the associated AP. */
int plant_wifi_rssi(int16_t *rssi_dbm);

/* Query CONFIG_PLANT_NET_SNTP_SERVER. On success, *epoch_ms is wall-clock time
 * in ms since the Unix epoch at the moment of return. */
int plant_sntp_sync(int32_t timeout_ms, int64_t *epoch_ms);

/* Resolve broker_host (IPv4 literal or DNS name), open TCP to
 * CONFIG_PLANT_NET_MQTT_PORT, send CONNECT with client_id, wait for CONNACK. */
int plant_mqtt_connect(const char *broker_host, const char *client_id,
		       int32_t timeout_ms);

/* Publish at QoS 0 (FR-MA-11). */
int plant_mqtt_publish(const char *topic, const char *payload, size_t len);

/* Process incoming packets and send a keepalive PINGREQ when due.
 * Returns 0, or -ENOTCONN if the connection dropped. */
int plant_mqtt_poll(int32_t timeout_ms);

/* Send DISCONNECT and close the socket. */
int plant_mqtt_disconnect(void);
```

#### SM-41: Behaviour
- **Blocking:** every call returns 0 on success or a negative errno (`-ETIMEDOUT` when the timeout expires).
- **No threads:** the module starts no threads of its own. The app's thread does the work inside the call.
- **Single client:** there is one MQTT client per device. The module is not thread-safe; calls come from one thread.
- **Explicit init:** apps call `plant_sensors_init()` and `plant_net_init()` themselves, in `main()`. There is no `SYS_INIT`, so the init order is visible in each app.
- **Arguments vs Kconfig:** credentials, broker host and client ID are arguments, because they differ per app. `main` reads them from `Settings`; baseline A has them at compile time. Everything that must be identical in both apps comes from the snippet's Kconfig.
- **Client ID:** both apps pass their `device_name` as the MQTT client ID.
- **ESP32-specific code:** the `esp_wifi_set_ps()` call in SM-16 is the only Espressif-specific code in the shared modules.

#### SM-42: Kconfig
```kconfig
config PLANT_NET
	bool "Plant network primitives (WiFi, MQTT, SNTP)"
	depends on WIFI && MQTT_LIB && SNTP && NET_DHCPV4 && DNS_RESOLVER

if PLANT_NET
# choice PLANT_NET_WIFI_PS (SM-16)
config PLANT_NET_MQTT_PORT
	int "MQTT broker TCP port"
	default 1883
config PLANT_NET_MQTT_BUFFER_SIZE
	int "MQTT client RX/TX buffer size (bytes, each)"
	default 512
config PLANT_NET_SNTP_SERVER
	string "SNTP server"
	default "pool.ntp.org"
endif
```

---

## What stays in each app (orchestration)

The shared primitives are called in different orders by design. These differences *are* the architecture under comparison and must not move into the shared modules.

| | `main` | baseline A (`active-wifi`) |
|---|---|---|
| WiFi | `plant_wifi_connect` / `plant_wifi_disconnect` every cycle | `plant_wifi_connect` once at boot; reconnect after a drop |
| MQTT | connect, publish `sensors/raw` + `status`, disconnect, every cycle | connect once; publish both topics every interval; `plant_mqtt_poll` while waiting |
| SNTP | every cycle (DD-MA-11) | once at boot; wall clock afterwards = boot epoch + `k_uptime_get()` |
| Credentials, broker, device name | `Settings` (provisioned over BLE) | compile time (FR-AW-05); `device_name = "baseline-a"` |
| Failure policy | timeout → discard and sleep (DD-MA-02/03) | reconnect, no backoff (FR-AW-04) |
| Timing and phase markers | FR-MA-16, FR-MA-18 | none |
| `status` fields | all (FR-MA-10) | `wifi_rssi_dbm` each publish; `cycle_count` = publish counter in RAM; `timestamp_ms` from the boot sync (`null` if it failed); `wake_reason` and every `t_*_ms` absent → `null` |

---

## Testing

#### SM-50: Host tests for the pure parts
`plant_json` and `plant_soil_pct()` are tested by plain C programs in `tests/host/`, built with WSL's `gcc` (no Zephyr):

```
wsl make -C tests/host
```

Minimum coverage:
- **`test_json.c`:**
  - FR-MA-10 example payloads reproduced byte for byte;
  - each validity/presence bit cleared on its own → that field is `null` and the others are unchanged;
  - all fields invalid;
  - `timestamp_ms == NULL`;
  - both wake-reason strings;
  - `-ENOMEM` on a buffer one byte too small;
  - negative RSSI;
  - `uint32_t` maximum values.
- **`test_soil_calib.c`:**
  - `raw == dry` → 0;
  - `raw == wet` → 100;
  - midpoint → 50;
  - raw beyond either end → clamped;
  - a calibration where `dry < wet` (inverted probe) still maps correctly.

Hardware-facing code (`sensors.c`, `soil.c`, `plant_net`) is validated on the bench. On-board ztest via `twister --device-testing` is deferred, as a later option.

---

## Verification on the device

Before the first measurement campaign, and after any change to the shield, the snippet or the shared modules:

1. Both apps build, and `build/zephyr/zephyr.dts` shows I2C0 on GPIO4/5 with the three sensor nodes and `channel@2`.
2. In both apps, `build/zephyr/.config` contains identical values for every `CONFIG_PLANT_*` symbol and every symbol in SM-12 and SM-14.
3. **`static-sleep`:** its `zephyr.map` contains no `plant_` symbol.
4. **On the bench:**
   - A `main` cycle publishes all seven sensor fields as non-null.
   - The INA219 trace shows current returning to its pre-sensor level when the `01` phase marker ends (SM-11).
   - Baseline A's idle current between publications shows the periodic DTIM wake pattern of `MIN_MODEM` rather than a flat receiver-on level (SM-16). Baseline A builds with `CONFIG_PM=n`. If its idle trace is flat, modem sleep is not taking effect without system power management, and baseline A has silently become the `NONE` case. That reopens [ADR-0006](../../adr/0006-comparison-baseline-same-stack.md); it is not a bench adjustment.

---

## Design decisions

### DD-SM-01: Repository as a Zephyr module, hardware as a shield, network settings as a snippet
See [ADR-0005](../../adr/0005-shared-modules-zephyr-module-shield-snippet.md).

### DD-SM-02: C API for all shared modules
`main` is C++17 and the baselines are C, and Zephyr's driver APIs are C. Shared modules are C with `extern "C"` guards in their headers. `main` embeds the C types directly (FR-MA-08) instead of mirroring them in C++.

### DD-SM-03: Soil moisture is a helper, not a sensor driver
A custom sensor driver would need a devicetree binding, driver Kconfig, an init priority after the ADC, private sensor channels for percent and raw ADC, and `sensor_value` fixed-point conversions. Each of these can fail quietly, as a device that is "not ready" with no reason given. The helper has about 40 lines, and its failures come back as error codes at the call site. Upstream's `voltage-divider` sensor driver was rejected because it exposes only millivolts and would break DD-MA-12's "raw ADC always published". Reconsider if a second analog probe is added.

### DD-SM-04: Validity bitmask, not NaN
`soil_moisture_raw_adc` is an integer and can't hold NaN, so NaN would need a second failure convention. One bitmask covers every field type and maps directly to JSON `null`.

### DD-SM-05: Three modules, split by dependency
`plant_json` has no hardware or network dependency. Keeping it separate lets it build on the host and makes the payload format an explicit contract. Splitting a module later would rename Kconfig symbols and headers in every app that uses it, so the split is fixed now.

### DD-SM-06: Network settings pinned by a snippet
A snippet's fragment is merged after `prj.conf`, so an app can't diverge from the shared stack by accident (SM-05). Module Kconfig defaults with a comparison script were rejected because they only *detect* divergence; hard-coded constants were rejected because they can't cover Zephyr's own symbols such as `CONFIG_MQTT_KEEPALIVE`.

---

## Out of scope

- On-board automated tests (deferred).
- Sharing the phase markers or the BLE name helper. Each has one user; the phase markers are kept C-compatible so they can move later.
- Static IP and any other `t_wifi` optimisation (SM-15).
- The compile-time default broker address (backend ticket 20). `plant_net` takes the broker as an argument and doesn't care where it comes from.

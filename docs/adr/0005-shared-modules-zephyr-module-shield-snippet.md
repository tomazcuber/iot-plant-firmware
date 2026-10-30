---
status: accepted
---

# Shared code is a Zephyr module, the sensor hardware is a shield, the network settings are a snippet

`apps/main` and comparison baseline A (`apps/active-wifi`) must do identical per-publish work. They need the same sensor read, the same payload encoding and the same network stack. The repository therefore is a Zephyr module (`zephyr/module.yml` at the root, with `board_root`, `dts_root` and `snippet_root` set). It provides three C libraries under `lib/`: `plant_sensors`, `plant_json` and `plant_net`.

The physical sensor set is described as a **shield** (`boards/shields/plant_sensors/`). The shield holds:
- the I2C0 move to GPIO4/5 and the three sensor nodes;
- the soil ADC channel;
- the sensor Kconfig, `BME280_MODE_FORCED` and the soil probe's calibration.

The shared network settings live in a **snippet** (`snippets/plant-net/`). An app opts in with `set(SHIELD plant_sensors)` and `set(SNIPPET plant-net)` in its `CMakeLists.txt`. Shield and snippet fragments are merged after `prj.conf`, so an app can't change a shared value by accident. The full contract is in [the shared-modules spec](../superpowers/specs/2026-10-30-shared-modules-design.md).

## Considered options

- **Packaging.** A plain `lib/` folder wired into each app by hand (`add_subdirectory`, a per-app `Kconfig` that `rsource`s the library, and a `BOARD_ROOT` line) was rejected. It needs three edits per app, any of which can be forgotten. It also has to solve board-root discovery app by app, which the module's `board_root` setting solves once.
- **Devicetree.** A custom board was rejected: it claims the sensors are part of the board, it diverges from the upstream `esp32c6_devkitc` target the specs promise, and it means tracking upstream board files on every Zephyr bump. Copied `app.overlay` files were rejected because they drift. A shared `.dtsi` included from each `app.overlay` was workable, but it can't carry Kconfig, so each app would repeat the sensor configuration.
- **Network settings.** Module Kconfig defaults plus a script comparing the apps' `.config` files would only *detect* divergence. Constants in C can't cover Zephyr's own symbols (`CONFIG_MQTT_KEEPALIVE`). The snippet *prevents* divergence.
- **Soil moisture.** A custom sensor driver was rejected in favour of a plain ADC helper, because a driver adds a binding, an init order and fixed-point conversions that fail quietly. Upstream's `voltage-divider` driver reports only millivolts, which would break publishing the raw ADC value (DD-MA-12).
- **Language.** C++ shared modules would force the C baselines to C++. The modules are C with `extern "C"` headers, and `main` embeds the C types directly.

## Consequences

- Supersedes the 2026-04-26 plan's custom `boards/esp32c6_devkitc/` and `drivers/soil_moisture/` west module. Neither was built. A `module.yml` under `drivers/soil_moisture/` would not have been discovered anyway: only `<project>/zephyr/module.yml` is.
- Every build in the workspace discovers the module, including `static-sleep` and `active-ble`. They compile nothing from it, because every `CONFIG_PLANT_*` symbol defaults to `n`. Check `static-sleep`'s `zephyr.map` for `plant_` symbols after the module lands.
- An app that needs to override a shared value has to do it deliberately, through `EXTRA_CONF_FILE`. For a measured build that breaks [ADR-0006](0006-comparison-baseline-same-stack.md).
- Adding a module later is additive: a new folder, one `add_subdirectory_ifdef` line and one `rsource` line. Splitting an existing module is not, so the three-module split is fixed now.

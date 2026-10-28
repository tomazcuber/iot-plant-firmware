# Development Environment Setup

**Platform:** Ubuntu 24.04 on WSL2 (Windows 11)  
**Target:** ESP32-C6 (RISC-V) · Zephyr RTOS v4.0 LTS  
**Workspace layout:** west T2 topology — `zephyr-workspace/` is the workspace root; `iot-plant-firmware/` (this repo) is the manifest repo inside it.

```
Projects/
└── zephyr-workspace/             ← west workspace root (.west/ lives here)
    ├── iot-plant-firmware/       ← this repo (manifest)
    ├── zephyr/                   ← fetched by west
    └── modules/                  ← fetched by west
```

All `west` commands are run from `iot-plant-firmware/` using the venv west binary. West walks up to find `.west/` automatically.

---

## Prerequisites check

| Tool | Required | Installed |
|---|---|---|
| Python | ≥ 3.10 | 3.12.3 ✓ |
| git | any | 2.43.0 ✓ |
| pip | any | 24.0 ✓ |
| cmake | ≥ 3.20 | 3.28.3 ✓ |
| ninja | any | 1.11.1 ✓ |
| dtc (device-tree-compiler) | any | 1.7.0 ✓ |
| gperf | any | 3.1 ✓ |
| ccache | any | ✓ |
| west | ≥ 1.2 | 1.5.0 ✓ |
| Zephyr SDK | 0.16.x | → Step 7 |

---

## Step 1 — System dependencies

```bash
sudo apt update && sudo apt install -y \
  python3-pip python3-venv \
  cmake ninja-build gperf ccache dfu-util \
  device-tree-compiler wget xz-utils file make \
  gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
```

---

## Step 2 — Create workspace and west venv

```bash
mkdir -p ~/Projects/zephyr-workspace
# move or clone iot-plant-firmware into zephyr-workspace/
cd ~/Projects/zephyr-workspace/iot-plant-firmware
python3 -m venv .venv
.venv/bin/pip install west
```

Activate for the session (or add to `~/.bashrc`):

```bash
source ~/Projects/zephyr-workspace/iot-plant-firmware/.venv/bin/activate
```

---

## Step 3 — west manifest (`west.yml`)

Pins Zephyr v4.0.0 LTS; `import: true` pulls all upstream HALs and modules.  
Already committed at `west.yml` in the repo root.

---

## Step 4 — Initialize west workspace

Run from inside the repo:

```bash
cd ~/Projects/zephyr-workspace/iot-plant-firmware
.venv/bin/west init -l .
```

Creates `zephyr-workspace/.west/config`. Zephyr and modules will be fetched as siblings of `iot-plant-firmware/`.

---

## Step 5 — Fetch Zephyr and all modules

```bash
.venv/bin/west update
```

Clones Zephyr v4.0.0 to `../zephyr/` and all HAL/module dependencies to `../modules/`. ~2 GB, takes 5–15 minutes.

---

## Step 6 — Python requirements

```bash
.venv/bin/pip install -r ../zephyr/scripts/requirements.txt
```

---

## Step 7 — Zephyr SDK (toolchain)

The Zephyr SDK bundles the RISC-V toolchain required for ESP32-C6 (`riscv64-zephyr-elf`).

```bash
cd ~
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_linux-x86_64.tar.xz
tar xf zephyr-sdk-0.16.8_linux-x86_64.tar.xz
cd zephyr-sdk-0.16.8
./setup.sh -t riscv64-zephyr-elf -c
```

`-t riscv64-zephyr-elf` installs only the RISC-V toolchain (saves ~1 GB vs full install).  
`-c` registers CMake packages so Zephyr auto-detects the SDK.

---

## Step 8 — ESP32 firmware blobs

Espressif's WiFi and BLE radio firmware (closed-source blobs) are required for WiFi and BLE builds:

```bash
cd ~/Projects/zephyr-workspace/iot-plant-firmware
.venv/bin/west blobs fetch hal_espressif
```

---

## Step 9 — Project structure

```
zephyr-workspace/
├── .west/
├── iot-plant-firmware/           ← this repo
│   ├── west.yml
│   ├── .venv/                    ← gitignored
│   ├── apps/
│   │   ├── main/                 ← thesis deliverable (C++)
│   │   ├── active-ble/           ← I_ble baseline measurement (C)
│   │   ├── active-wifi/          ← I_wifi baseline measurement (C)
│   │   └── static-sleep/         ← I_sleep baseline measurement (C)
│   ├── boards/
│   │   └── esp32c6_devkitc/      ← shared board definition
│   ├── drivers/
│   │   └── soil_moisture/        ← custom ADC driver
│   └── docs/
├── zephyr/                       ← fetched by west
└── modules/                      ← fetched by west
```

---

## Step 10 — Verify a build

Build `static-sleep` first — it has no radio stack and is the fastest to compile:

```bash
cd ~/Projects/zephyr-workspace/iot-plant-firmware
.venv/bin/west build -b esp32c6_devkitc/esp32c6 apps/static-sleep -- -DBOARD_ROOT=${PWD}
```

A successful build produces `build/zephyr/zephyr.bin`.

---

## Flashing (WSL2)

The board plugs into **Windows**, so WSL2 can't see it until the USB device is
forwarded in with [`usbipd-win`](https://github.com/dorssel/usbipd-win). The
ESP32-C6's native USB-Serial/JTAG enumerates as `303a:1001`
("USB JTAG/serial debug unit") and shows up inside WSL as **`/dev/ttyACM0`**
(not `ttyUSB0`).

### Step 1 — Forward the USB device into WSL

One-time install on Windows (**Admin PowerShell**):

```powershell
winget install usbipd
# close and reopen the Admin PowerShell so usbipd lands on PATH
```

Each session (after every replug or reboot):

```powershell
usbipd list                          # find the 303a:1001 BUSID, e.g. 2-3
usbipd bind   --busid 2-3            # one-time per device (needs admin)
usbipd attach --wsl --busid 2-3      # redo after every replug/reboot
```

Confirm inside WSL — `dmesg | tail` should end with
`cdc_acm 1-1:1.0: ttyACM0: USB ACM device` and `/dev/ttyACM0` should exist.

### Step 2 — Grant serial-port access

The node comes up `root:dialout`. Make membership permanent (do this once in a
real terminal, then `wsl --shutdown` from Windows and reopen):

```bash
sudo usermod -aG dialout $USER
```

Until that takes effect — or if `dialout` membership ever isn't active — grant
access to the current node directly (must be redone after each `usbipd attach`,
since the node is recreated `root:dialout`):

```bash
sudo chmod a+rw /dev/ttyACM0
```

### Step 3 — Flash

```bash
.venv/bin/west flash
```

Uses the `esp32` runner with `esptool.py` from `hal_espressif`. esptool probes
every serial port; ignore the `permission denied` lines for `/dev/ttyS0`/`ttyS1`
— only `/dev/ttyACM0` matters. A good flash ends with `Hash of data verified.`
and `Hard resetting via RTS pin...`.

**Gotcha — "required program esptool.py not found":** the bundled
`../modules/hal/espressif/tools/esptool_py/esptool.py` may arrive without the
execute bit, which makes Zephyr's runner (`shutil.which`) report it missing even
though the file exists. Fix once:

```bash
chmod +x ../modules/hal/espressif/tools/esptool_py/esptool.py
```

A `west update` that re-fetches `hal_espressif` can reset this — just redo it.

### Serial monitor

```bash
.venv/bin/west espressif monitor
```

Note: the baseline firmwares disable the console (e.g. `static-sleep` sets
`CONFIG_CONSOLE=n` / `CONFIG_UART_CONSOLE=n`), so they produce **no serial
output by design** — a silent monitor is expected, not a failed flash. Their
output is the `Current draw` figure measured off-device with a multimeter.

---

## `.gitignore`

```
# Python venv
.venv/

# west build output
build/

# credentials — never commit
apps/active-wifi/credentials.conf
```

`zephyr/`, `modules/`, and `.west/` live in the parent `zephyr-workspace/` directory and are not part of this repo.

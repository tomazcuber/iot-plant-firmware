# Migrating the workspace from WSL to native Windows

Rationale: flashing the ESP32-C6 over WSL requires `usbipd` USB passthrough,
which is unreliable; the Zephyr docs discourage WSL for this reason. Native
Windows sees the board's COM port directly.

This is a **non-destructive** migration. The WSL environment is left fully
intact as a fallback until the Windows host is verified.

## What travels, and how

| Data | Channel | Notes |
|---|---|---|
| `iot-plant-firmware` git repo + history | GitHub | `main` + `migration-wip` branches pushed |
| In-progress dirty-tree work | GitHub | captured in the `migration-wip` branch |
| `apps/active-wifi/credentials.conf` (gitignored) | hand-carry | secret, not on GitHub |
| repo `.claude/` + workspace `.claude/settings.local.json` (gitignored) | hand-carry | local agent config |
| `zephyr/`, `modules/`, `bootloader/`, `tools/` (~7 GB) | regenerated | `west update` re-downloads |
| `.venv/`, `build/`, Zephyr SDK | regenerated | Linux binaries — never copy |

**Hand-carry folder** (staged on the WSL side):
`\\wsl.localhost\Ubuntu\home\tomazcuber\migration-handoff\` — see its `README.txt`.

---

## Phase 2 — Windows host setup (PowerShell)

Target layout: a **short** root path `C:\zw` — Zephyr build trees are deep and
hit the Windows `MAX_PATH` limit under long roots like `C:\Users\...`.

### 1. Install prerequisites (Chocolatey)

Run PowerShell **as Administrator**:

```powershell
Set-ExecutionPolicy Bypass -Scope Process -Force; `
  [System.Net.ServicePointManager]::SecurityProtocol = 3072; `
  iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

choco install -y cmake ninja gperf python git dtc-msys2 wget 7zip
```

Close and reopen PowerShell (non-admin is fine from here) so PATH updates.

> winget alternative: `winget install Kitware.CMake Ninja-build.Ninja Python.Python.3.12 Git.Git`, but `gperf`/`dtc` are easiest via Chocolatey.

### 2. USB driver for the ESP32-C6-DevKitC

- The board's **native USB port** enumerates as a *USB Serial Device* on
  Windows 10/11 with **no driver** — try this port first.
- If you use the **UART port** (bridge chip), install the matching driver:
  - Silicon Labs CP210x, or
  - WCH CH34x
- Verify in **Device Manager → Ports (COM & LPT)**; note the `COMx` number.

### 3. Avoid CRLF corruption (before cloning)

```powershell
git config --global core.autocrlf false
```

### 4. Create the West workspace

```powershell
py -3 -m venv C:\zw\.venv
C:\zw\.venv\Scripts\Activate.ps1
pip install west

west init -m https://github.com/tomazcuber/iot-plant-firmware --mr main C:\zw
cd C:\zw
west update            # re-downloads zephyr v4.0.0 + modules (~7 GB)
west zephyr-export
west packages pip --install
```

### 5. Toolchain + Espressif blobs

```powershell
west sdk install                     # picks the SDK matching Zephyr v4.0.0
west blobs fetch hal_espressif       # REQUIRED for ESP32 builds
```

> If `west sdk install` is unavailable, download the Zephyr SDK 0.17.x Windows
> installer from the zephyr-sdk-ng releases and run `setup.cmd`.

### 6. Restore your in-progress work

```powershell
cd C:\zw\iot-plant-firmware
git checkout migration-wip
```

Then copy the three hand-carry files from
`\\wsl.localhost\Ubuntu\home\tomazcuber\migration-handoff\` into place:

- `repo\apps\active-wifi\credentials.conf` → `C:\zw\iot-plant-firmware\apps\active-wifi\credentials.conf`
- `repo\.claude\*` → `C:\zw\iot-plant-firmware\.claude\`
- `workspace\.claude\settings.local.json` → `C:\zw\.claude\settings.local.json` (workspace root, **not** inside the repo)

### 7. Verify — the whole point of the migration

```powershell
cd C:\zw
west build -b esp32c6_devkitc/esp32c6/hpcore iot-plant-firmware/apps/static-sleep -p always
$env:PATHEXT += ";.PY"               # see note below — required on Windows before `west flash`
west flash                           # add --esp-device COMx if auto-detect fails
```

> Windows `west flash` gotcha: Zephyr's ESP32 runner calls
> `shutil.which("…\esptool_py\esptool.py")` to validate the tool. On Windows,
> `shutil.which` only recognizes files whose extension is in `PATHEXT`, and
> `.PY` is NOT in the default `PATHEXT`. Without the extension registered,
> the runner reports `esptool.py not found` even though the file exists.
> Prepend `.PY` to `PATHEXT` for the session (or permanently in your user
> env vars) before running `west flash`.

> Zephyr v4.2 renamed the ESP32-C6 board target to require an explicit CPU
> qualifier — use `esp32c6_devkitc/esp32c6/hpcore` for application code (the
> high-performance RISC-V core). The `/lpcore` variant targets the ULP
> coprocessor.

> Note on Zephyr version: `west.yml` was bumped from v4.0.0 → **v4.2.2**
> during Windows migration. Reason: Zephyr v4.0.0's picolibc retarget-lock
> hooks are incompatible with Zephyr SDK 0.17.2+ (the SDK changed `_LOCK_T`
> from `void *` to `struct __lock *`). `west sdk install` on Windows now
> ships 0.17.4, so v4.0.0 fails to build. The fix landed in Zephyr v4.2.0.

A successful build **and** flash confirms the migration. Watch serial output:

```powershell
west espressif monitor              # or any serial terminal at 115200 baud on COMx
```

---

## After verification

- Re-organize `migration-wip` into proper commits, or merge/rebase onto `main`
  and delete the temporary branch.
- Only once Windows build+flash is confirmed, reclaim WSL space if desired
  (the ~7 GB is in `~/Projects/zephyr-workspace/{zephyr,modules}` + `.venv`).

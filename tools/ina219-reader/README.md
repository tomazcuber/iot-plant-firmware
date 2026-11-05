# INA219 current reader

Bench instrument that records a device-under-test's current trace. Spec:
`docs/superpowers/specs/2026-11-03-ina219-reader-design.md`; decision:
`docs/adr/0007-reader-records-raw-gap-free-samples.md`. Not a measured build:
it uses no `plant_*` module, shield or snippet.

## Firmware

Second nanoESP32-C6, native USB port (USB-Serial/JTAG) to the PC. Wiring is in
the spec's Bench section (I2C GPIO4/5, markers GPIO6 = bit 1, GPIO7 = bit 0).

```bash
west build -b esp32c6_devkitc/esp32c6/hpcore -d build-reader tools/ina219-reader
west flash -d build-reader
```

The `# id` git id is regenerated on every build (`reader_git.h`), so it follows
commits; `-dirty` means uncommitted changes.

Console/logging stay on UART0. Protocol: `id`, `vbus`, `start trace|averaged`,
`stop`; data lines are `seq,t_us,shunt_raw,phase`. Samples lost before the host are counted (`# ovf`, `ovf=`) apart from failed I2C reads (`# i2c_err`, `i2c=`); `vbus` replies `# err timeout` if no conversion completes in 150 ms. In the Averaged configuration `seq` counts
emitted results (unchanged results are not emitted and not counted).

## Host recorder (native Windows Python + pyserial)

```bash
pip install pyserial
python tools/ina219-reader/host/record.py --port COM7 --config trace --app net-smoke --run 1 --seconds 120
```

Start each measurement session with a zero run (VIN- open, no load):

```bash
python tools/ina219-reader/host/record.py --port COM8 --zero --app acceptance --run 1 --seconds 10
```

Later runs of the session are converted with the latest zero (within 12 h, same output directory); `--offset-lsb` overrides. If the recorder exits non-zero it still wrote the files; the warnings say what failed.

Output goes to `C:\zw\bench-data\` (override with `--out` or `INA219_DATA_DIR`).
`current_mA = (shunt_raw - offset_lsb) * 0.01 mV / R_eff`, R_eff from `calibration.json` (ADR-0008).

Tests: `python -m unittest discover -s tools/ina219-reader/host`

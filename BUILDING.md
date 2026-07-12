# Building and flashing

This repository uses one pinned build path for the DUT and passive counter.
Return to the [README](README.md) for operation or continue to
[TESTING.md](TESTING.md) after both boards are flashed.

## Pinned toolchain

| Component | Version or value |
| --- | --- |
| Arduino CLI | 1.5.1 |
| ESP8266 core | 3.1.2 |
| Board FQBN | `esp8266:esp8266:d1_mini_lite` |

The local script rejects another installed ESP8266 core version so local output
does not silently differ from CI.

## Install prerequisites

Install [Arduino CLI](https://arduino.github.io/arduino-cli/) and make
`arduino-cli` available on `PATH`.

Install the pinned ESP8266 core:

```powershell
arduino-cli core update-index --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core install esp8266:esp8266@3.1.2 --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
```

Verify the installation:

```powershell
arduino-cli version
arduino-cli core list
```

## Compile both sketches

Run from the repository root:

```powershell
./scripts/build.ps1
```

The script compiles:

- `wemos-wifi-barodcaster/`
- `test-tools/beacon-counter/`

It prints the normal compiler output plus a compact flash/IRAM table. To also
write a Markdown report:

```powershell
./scripts/build.ps1 -SummaryPath ./firmware-memory.md
```

`firmware-memory.md` is an optional generated report; remove or relocate it
when you do not want it in the working tree.

## Upload

List attached boards:

```powershell
arduino-cli board list
```

Upload the DUT, replacing `COM12` with its port:

```powershell
arduino-cli upload --fqbn esp8266:esp8266:d1_mini_lite --port COM12 wemos-wifi-barodcaster
```

Upload the counter, replacing `COM13` with its port:

```powershell
arduino-cli upload --fqbn esp8266:esp8266:d1_mini_lite --port COM13 test-tools/beacon-counter
```

Upload does not erase the DUT's emulated EEPROM. A saved flood-on state can
resume after reset, so flash and power the DUT only inside RF isolation.

## Arduino IDE alternative

1. Install ESP8266 board package 3.1.2.
2. Open either the
   [main sketch](wemos-wifi-barodcaster/wemos-wifi-barodcaster.ino) or
   [counter sketch](test-tools/beacon-counter/beacon-counter.ino).
3. Select **LOLIN(WEMOS) D1 mini Lite**.
4. Select the correct serial port and upload.

Use the CLI script before merging changes even when developing in Arduino IDE;
it compiles both sketches with the same versions used by CI.

## CI

The [Arduino build workflow](.github/workflows/arduino-build.yml) runs on pushes
to `master` and pull requests targeting `master`. It:

1. Installs checksum-verified Arduino CLI 1.5.1.
2. Installs ESP8266 core 3.1.2.
3. Runs `scripts/build.ps1` for both sketches.
4. Adds flash/IRAM measurements to the job summary.
5. Retains the Markdown memory report as a 30-day artifact.

View current and previous runs on the repository's
[Actions page](https://github.com/kolec94/wemos-wifi-barodcaster/actions/workflows/arduino-build.yml).

## Memory baseline

Measured with the pinned toolchain and FQBN:

| Sketch | Flash | IRAM |
| --- | ---: | ---: |
| Main firmware | 279008 / 1048576 bytes (26%) | 59867 / 65536 bytes (91%) |
| Beacon counter | 237380 / 1048576 bytes (22%) | 59843 / 65536 bytes (91%) |

These values are comparison baselines, not enforced budgets. IRAM is already at
91%, so review changes that increase it and compare the CI report before merge.

## Troubleshooting

### Wrong or missing core

If `scripts/build.ps1` reports a version other than 3.1.2, inspect installed
cores with `arduino-cli core list`, then install the pinned version using the
command above.

### Port disappears during upload

Windows may re-enumerate an ESP8285 under a different COM number after reset.
Run `arduino-cli board list` again and retry with the new port. Also verify that
the USB cable supports data and is secure.

### Port is busy

Close Arduino Serial Monitor, the Web Serial dashboard, and any other terminal
that owns the port before uploading. Reopen the dashboard after upload.

# Building

The repository compiles both Arduino sketches for a LOLIN(WEMOS) D1 mini Lite
using a pinned ESP8266 core.

## Prerequisites

Install [Arduino CLI](https://arduino.github.io/arduino-cli/) and make
`arduino-cli` available on `PATH`. Then install the pinned ESP8266 core:

```powershell
arduino-cli core update-index --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core install esp8266:esp8266@3.1.2 --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
```

The build script intentionally fails when another ESP8266 core version is
installed, preventing local builds from silently using a different toolchain.

## Compile both sketches

From the repository root, run:

```powershell
./scripts/build.ps1
```

This compiles:

- `wemos-wifi-barodcaster/`
- `test-tools/beacon-counter/`

Both use FQBN `esp8266:esp8266:d1_mini_lite` and ESP8266 core `3.1.2`.
Arduino CLI prints the flash and IRAM usage after each successful compilation.
The script also prints a compact table so changes are easy to compare. To write
the same table as Markdown, pass a report path:

```powershell
./scripts/build.ps1 -SummaryPath ./firmware-memory.md
```

The same script runs for pull requests and pushes to `master` in GitHub Actions.
CI adds the table to the job summary and retains it as the
`firmware-memory-usage` artifact for 30 days.

## Initial memory baseline

Measured with Arduino CLI `1.5.1`, ESP8266 core `3.1.2`, and FQBN
`esp8266:esp8266:d1_mini_lite`:

| Sketch | Flash | IRAM |
| --- | ---: | ---: |
| Main firmware | 279008 / 1048576 bytes (26%) | 59867 / 65536 bytes (91%) |
| Beacon counter | 237380 / 1048576 bytes (22%) | 59843 / 65536 bytes (91%) |

These values are a comparison baseline, not an enforced size budget. The build
collects the measurements in one place so a threshold can be added later after
normal variation is understood.

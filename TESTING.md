# Hardware testing

This guide validates the DUT with a second Wemos D1 Mini acting as a passive
measurement instrument. Start with [BUILDING.md](BUILDING.md) if the sketches
are not compiled and flashed yet. Return to the [README](README.md) for normal
operation and API details.

## Safety and isolation

Run every transmitting test inside a proper RF shield box or screen room.
Isolation is both a safety requirement and a measurement requirement: ambient
beacons contaminate the counter and make the results ambiguous.

- Put the DUT, counter, phone/adapter antenna, and all RF paths inside the
  isolated volume before powering the DUT.
- Use USB feedthroughs for serial and power.
- Confirm the DUT is stopped before opening the enclosure.
- Do not treat a foil-lined container or improvised metal box as verified RF
  containment.

## Test rig

| Role | Hardware | Firmware or tool |
| --- | --- | --- |
| DUT | Wemos D1 Mini Lite #1 | [Main firmware](wemos-wifi-barodcaster/wemos-wifi-barodcaster.ino) |
| Instrument | Wemos D1 Mini Lite #2 | [Passive counter](test-tools/beacon-counter/beacon-counter.ino) |
| Serial observer | Laptop | [Two-port dashboard](test-tools/serial-dashboard/index.html) |
| UI client | Phone or dedicated WiFi adapter | Connects only to `WifiBroadcaster` |

The instrument is receive-only. It groups a beacon as matching when the SSID
starts with the configured base SSID; this accommodates the DUT's changing
1-4-byte suffix.

## Preflight

1. Record the firmware commit:

   ```powershell
   git rev-parse --short HEAD
   ```

2. Compile both sketches:

   ```powershell
   ./scripts/build.ps1
   ```

3. Find the two serial ports:

   ```powershell
   arduino-cli board list
   ```

4. Flash the main firmware to the DUT and the counter firmware to the
   instrument. Exact commands are in [BUILDING.md](BUILDING.md#upload).
5. Open the [serial dashboard](test-tools/serial-dashboard/index.html) directly
   in Chrome or Edge. Connect the DUT and instrument at 115200 baud and enable
   timestamps.
6. Close any other serial monitor first. A serial port can have only one owner.
7. Establish dashboard connections before the timed run. A reconnect breaks
   log continuity and can reset some USB/serial board combinations.

## Configure the instrument

Send commands in the instrument panel:

```text
ssid TargetSSID
channel 6
show
```

Available commands:

| Command | Result |
| --- | --- |
| `show` | Prints the active target and channel |
| `ssid <name>` | Sets a 1-31-byte SSID prefix |
| `channel <1-11>` | Retunes immediately |
| `help` | Lists commands |

Settings are volatile and return to `TargetSSID`/channel 6 after reboot.

## Standard burst-500 soak

1. Confirm containment is closed.
2. Join `WifiBroadcaster` from the UI client and open
   `http://192.168.4.1`.
3. If the DUT resumed flooding from EEPROM, stop it before continuing.
4. Save SSID `TargetSSID`, channel 6, and burst 500.
5. Confirm the DUT serial log contains:

   ```text
   Config updated - SSID: TargetSSID  ch: 6  burst: 500
   Flooding OFF
   ```

   The firmware prints a Unicode dash in the actual `Config updated` message;
   the ASCII form above is shown for terminals that do not decode UTF-8.

6. Confirm the instrument reports `0 match/s` while stopped.
7. Start flooding and record the start time.
8. Run for at least 30 minutes. Keep both serial panels connected.
9. During the run, verify a steady nonzero `match/s`, an accessible UI, and no
   additional boot banner or reset cause.
10. Stop flooding and confirm `match/s` immediately returns to zero while
    `total_match` remains fixed.

### UI client behavior

Phones often leave open networks that do not provide internet, especially when
the screen locks. For an association test, use one of these:

- A dedicated WiFi adapter with continuous `/status` polling.
- An iPhone in Airplane Mode with WiFi re-enabled, Auto-Lock set to Never, and
  the screen kept awake.
- An Android device configured to stay on a network without internet and kept
  awake.

A phone roaming away while locked is client behavior, not proof of an AP drop.
It does, however, invalidate the association portion of that run.

## Read the counter output

The counter prints:

```text
match/s  other/s  |  total_match  total_other
```

Example from the verified burst-500 run:

```text
   947        10    |      169435       217962
   934        10    |      170369       217972
```

- `match/s`: target-prefix beacons received during the last second.
- `other/s`: all other beacons, including the DUT's `WifiBroadcaster` AP.
- `total_match` and `total_other`: cumulative counts since counter boot.

In the verified isolated setup, burst 500 produced roughly 930-960 matching
beacons/sec. The config AP contributed roughly 9-11 other beacons/sec. Treat a
large unexplained increase in `other/s` as possible RF leakage or an unexpected
transmitter; do not expect `other/s` to be zero while the config AP is running.

## Test matrix

| ID | Validates | Procedure | Pass criteria |
| --- | --- | --- | --- |
| T1 | Watchdog survival | Run the standard burst-500 soak for 30+ minutes | One boot banner; no `rst cause:4`; no stall |
| T2 | UI responsiveness | Request `/status` repeatedly during T1 | Responses remain prompt (target under 500 ms); no timeouts |
| T3 | Config AP stability | Keep a non-sleeping client associated throughout T1 | No DUT-caused disassociation; client remains able to reload the UI |
| T4 | Emission correctness | Observe the counter during T1 | Steady nonzero `match/s`; only expected config-AP traffic in `other/s` |
| T5 | Channel changes | Save SSID-only, then channel-only changes | SSID change keeps client; channel change causes one expected reconnect and no DUT reset |
| T6 | Channel range | Inspect UI and submit invalid values | UI offers 1-11; API rejects values outside 1-11 |
| T7 | No-op save | Save current values again | API returns `changed:false`; no config-update serial message or EEPROM commit path |
| T8 | Resume on boot | Start, power-cycle inside isolation, then observe | DUT resumes; counter receives matching traffic; no UI action required |
| T9 | Atomic burst rejection | Submit valid SSID/channel with burst 501 | HTTP 400; no submitted fields change |

Burst 500 is the supported maximum. T1-T4 qualify runtime behavior at that
limit. T9 proves that a value above the limit cannot partially update otherwise
valid fields.

## API boundary test

This test requires a computer connected to `WifiBroadcaster`. Connecting the
computer may interrupt its normal internet connection; use a dedicated adapter
when that matters.

Record the current status:

```powershell
curl.exe http://192.168.4.1/status
```

Submit the invalid atomic update:

```powershell
curl.exe -i -X POST `
  -H "Content-Type: application/x-www-form-urlencoded" `
  --data "ssid=ShouldNotApply&channel=11&burst=501" `
  http://192.168.4.1/set_config
```

Expected response:

```text
HTTP/1.1 400 Bad Request
{"ok":false,"errors":{"burst":"Burst size must be an integer from 1 to 500"}}
```

Request `/status` again and verify the SSID, channel, and burst all retain their
previous values.

## Recorded hardware baseline

Hardware acceptance run on 2026-07-12:

| Item | Result |
| --- | --- |
| Firmware commit | `8232b80` |
| Hardware | Two ESP8285 Wemos D1 Mini Lite boards |
| Configuration | `TargetSSID`, channel 6, burst 500 |
| Duration | Approximately 29 minutes |
| Matching rate | Approximately 930-960 beacons/sec |
| Resets/stalls | None reported |
| Final `total_match` | 2,038,237 |
| Stop behavior | `match/s` immediately became 0 and total remained fixed |

The iPhone UI client left the no-internet AP when its screen locked. That was
identified as client sleep/roaming behavior rather than a DUT reset. Future T3
runs should use a non-sleeping client as described above.

## Record a run

Capture this information for each hardware session:

```text
Commit:
ESP8266 core:
DUT port / instrument port:
SSID / channel / burst:
Start / stop / duration:
Reset count and rst-cause lines:
Mean or representative match/s:
Typical other/s:
UI latency or reconnect observations:
Final total_match:
Containment notes:
```

For longer investigations, add explicit uptime or heap instrumentation in a
temporary test build. The production firmware does not currently print a
periodic uptime/heap status line.

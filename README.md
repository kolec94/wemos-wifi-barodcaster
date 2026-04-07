# Wemos D1 Mini Lite - WiFi Beacon Flooder

> **For use in controlled lab environments only.**

Floods a target WiFi channel with 802.11 beacon frames for a specified SSID. Each beacon is injected with a fresh random locally-administered MAC address, making the target SSID appear as many distinct access points and saturating the beacon space on that channel. Configured via a built-in captive portal web UI — no router needed.

**Stack:** Arduino C/C++, ESP8266 raw packet injection (`wifi_send_pkt_freedom`), DNSServer, ESP8266WebServer, EEPROM

## How It Works

```mermaid
flowchart TD
    A[Power on Wemos D1 Mini] --> B[Load config from EEPROM\ntarget SSID, channel, flood state]
    B --> C[WiFi.mode WIFI_AP\nsoftAP: open 'WifiBroadcaster'\non configured channel]
    C --> D[Start DNS server\nredirect all queries → 192.168.4.1]
    D --> E[Start web server port 80]
    E --> F[prepareBeaconTemplate\nbuild 802.11 beacon frame\nwith target SSID + channel tag]
    F --> G{flooding flag set\nin EEPROM?}
    G -->|Yes| H[Resume flooding on boot]
    G -->|No| I[Wait for user to\nstart via web UI]
    H & I --> J[loop runs continuously]

    J --> K{flooding = true?}
    K -->|Yes| L[sendBeaconBurst\n50 x wifi_send_pkt_freedom\neach with fresh random MAC]
    L --> K
    K -->|No| M[dnsServer.processNextRequest]
    L --> M
    M --> N[server.handleClient]
    N --> J

    N --> O{Request?}
    O -->|GET /| P[Serve web UI]
    O -->|GET /status| Q[Return JSON status]
    O -->|POST /set_config| R[Update SSID + channel\nrebuild beacon template\nrestart softAP on new channel]
    O -->|POST /toggle| S{Toggle flood state}
    S -->|ON| T[prepareBeaconTemplate\nstart flooding]
    S -->|OFF| U[Stop flooding]
    O -->|Any other URL| V[302 → http://192.168.4.1/]
    R & T & U --> W[Save to EEPROM]
```

## Beacon Frame Structure

Each injected frame is a standard 802.11 beacon:

| Section | Size | Notes |
|---------|------|-------|
| MAC header | 24 bytes | Frame type = management/beacon; DA = broadcast; SA + BSSID = random per frame |
| Beacon fixed fields | 12 bytes | Interval = 100 TUs; Capability = ESS |
| Tag 0: SSID | 2 + n bytes | The configured target SSID |
| Tag 1: Supported Rates | 10 bytes | 1, 2, 5.5, 11, 18, 24, 36, 54 Mbps |
| Tag 3: DS Parameter Set | 3 bytes | Declares the configured channel |

MAC addresses use locally-administered unicast format (bit 1 set, bit 0 clear in the first octet) and are re-randomised for every single frame.

## Hardware Requirements

- Wemos D1 Mini Lite (ESP8266 / ESP8285)
- USB cable for programming and power

## Software Requirements

- Arduino IDE 1.8.x or newer
- ESP8266 Board Package 3.x (includes `user_interface.h` with `wifi_send_pkt_freedom`)

### Installing ESP8266 Board Package

1. Open Arduino IDE → **File → Preferences**
2. Add to "Additional Board Manager URLs":
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
3. **Tools → Board → Boards Manager** → search "esp8266" → install

## Installation

1. Open `wifi_broadcaster.ino` in Arduino IDE
2. **Tools → Board → ESP8266 Boards → LOLIN(WEMOS) D1 mini Lite**
3. **Tools → Port** → select the Wemos port
4. Upload (→)

No credentials need editing — everything is configured at runtime.

## Usage

### First Time Setup

1. Power on the device
2. Connect to the open network **`WifiBroadcaster`**
3. Captive portal redirects to the config UI (or navigate to `http://192.168.4.1`)

### Configuration

| Field | Description |
|-------|-------------|
| **Target SSID** | The SSID name to flood (max 31 chars) |
| **Channel** | WiFi channel to inject on (1–13) |

1. Enter the target SSID and select the channel
2. Click **Save Config**
3. Click **Start Flooding**

The status badge changes from `STOPPED` → `FLOODING`. The device injects 50 beacon frames per loop iteration, each with a unique random MAC.

### Reconfiguring While Flooding

The config AP (`WifiBroadcaster`) stays up on the same channel throughout. Connect to it and visit `192.168.4.1` to change settings or stop flooding at any time.

Flood state is persisted to EEPROM — the device resumes flooding on the last-used SSID/channel after a power cycle.

## Configuration Options

### Beacons Per Burst

Controls how many frames are injected before the main loop yields for DNS/HTTP:

```cpp
#define BEACONS_PER_BURST 50
```

Higher values = more aggressive flooding but slightly less responsive web UI.

### Default Setup AP Name

```cpp
const char* CONFIG_AP_SSID = "WifiBroadcaster";
```

### Default Channel (applied on first boot / EEPROM wipe)

```cpp
config.channel = 6;
```

## Technical Notes

- `wifi_send_pkt_freedom()` injects raw 802.11 frames directly, bypassing the normal WiFi stack
- The radio transmits on whichever channel `softAP` is configured to — `set_config` restarts the softAP when the channel changes to keep them in sync
- The ESP8266 has a single radio; it cannot simultaneously flood one channel while accepting connections on a different channel. The config AP and the flood channel are always the same
- `BEACONS_PER_BURST 50` injects roughly 50 frames per ms range depending on frame size and RF conditions

## Legal Notice

Beacon flooding disrupts the WiFi environment on the target channel for all nearby devices. **Only operate this device inside a properly RF-isolated lab.** Uncontrolled use violates FCC Part 15, Ofcom regulations, and equivalent rules in other jurisdictions.

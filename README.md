# Wemos D1 Mini Lite - WiFi Beacon Flooder

> **For use in controlled lab environments only.**

Floods a target WiFi channel with 802.11 beacon frames for a specified SSID. Each beacon is injected with a fresh random locally-administered MAC address, making the target SSID appear as many distinct access points and saturating the beacon space on that channel. Configured via a built-in captive portal web UI — no router needed.

**Stack:** Arduino C/C++, ESP8266 raw packet injection (`wifi_send_pkt_freedom`), DNSServer, ESP8266WebServer, EEPROM

## How It Works

```mermaid
flowchart TD
    A[Power on Wemos D1 Mini] --> B[Load config from EEPROM\ntarget SSID, channel, flood state]
    B --> C[WiFi.mode WIFI_AP_STA\nWiFi.disconnect — STA active\nbut never associates]
    C --> D[softAP: open 'WifiBroadcaster'\non configured channel]
    D --> E[Start DNS server\nredirect all queries → 192.168.4.1]
    E --> F[Start web server port 80]
    F --> G[prepareBeaconTemplate\nbuild 802.11 beacon frame\nwith target SSID + channel tag]
    G --> H{flooding flag\nset in EEPROM?}
    H -->|Yes| I[wifi_promiscuous_enable 1\nResume flooding on boot]
    H -->|No| J[Wait for user\nvia web UI]
    I & J --> K[loop runs continuously]

    K --> L{flooding = true?}
    L -->|Yes| M[sendBeaconBurst\n50 x wifi_send_pkt_freedom\nfresh random MAC per frame]
    M --> L
    L -->|No| N[dnsServer.processNextRequest]
    M --> N
    N --> O[server.handleClient]
    O --> K

    O --> P{Request?}
    P -->|GET /| Q[Serve web UI\nno auto-refresh polling]
    P -->|GET /status| R[Return JSON status]
    P -->|POST /set_config| S[Update SSID + channel\nrebuild beacon template\nrestart softAP on new channel]
    P -->|POST /toggle| T{New flood state?}
    T -->|ON| U[prepareBeaconTemplate\nwifi_promiscuous_enable 1\nstart flooding]
    T -->|OFF| V[wifi_promiscuous_enable 0\nstop flooding]
    P -->|Any other URL| W[302 → http://192.168.4.1/]
    S & U & V --> X[Save to EEPROM]
```

## Beacon Frame Structure

Each injected frame is a standard 802.11 management beacon:

| Section | Size | Notes |
|---------|------|-------|
| MAC header | 24 bytes | Frame type = management/beacon; DA = broadcast; SA + BSSID = random per frame |
| Beacon fixed fields | 12 bytes | Interval = 100 TUs; Capability = ESS + short preamble |
| Tag 0: SSID | 2 + n bytes | The configured target SSID |
| Tag 1: Supported Rates | 10 bytes | 1, 2, 5.5, 11, 18, 24, 36, 54 Mbps |
| Tag 3: DS Parameter Set | 3 bytes | Declares the configured channel |

MAC addresses use locally-administered unicast format (bit 1 set, bit 0 clear in the first octet) and are re-randomised for every single frame.

## Hardware Requirements

- Wemos D1 Mini Lite (ESP8266 / ESP8285)
- USB cable for programming and power

## Software Requirements

- Arduino IDE 1.8.x or newer
- ESP8266 Board Package 3.x — provides `user_interface.h`, `libmain.a` (contains `wifi_send_pkt_freedom`), and `libnet80211.a` (contains `ieee80211_freedom_output`)

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

### Verifying the Flood

The phone's built-in WiFi settings deduplicate entries by SSID name — they will always show one row regardless of how many BSSIDs are broadcasting. To see the individual injected frames you need a tool that displays BSSIDs directly:

| Tool | Platform |
|------|----------|
| WiFi Analyzer (open source) | Android |
| WiFi Explorer | macOS |
| `airport -s` in Terminal | macOS |
| Wireshark (monitor mode, filter `wlan.fc.type_subtype == 8`) | Any |

### Reconfiguring While Flooding

The config AP (`WifiBroadcaster`) stays up on the same channel throughout. Connect to it and visit `192.168.4.1` to change settings or stop flooding. The web UI updates only on page load and after user actions — there is no background polling.

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

### WiFi Mode

The device runs in `WIFI_AP_STA` mode. `WIFI_AP` alone suppresses raw frame injection on this SDK version — the STA layer must be initialised for `wifi_send_pkt_freedom` to activate the PHY. `WiFi.disconnect()` prevents the STA from ever scanning or associating.

### Promiscuous Mode

`wifi_promiscuous_enable(1)` is called when flooding starts and `wifi_promiscuous_enable(0)` when it stops. Enabling promiscuous mode bypasses the SDK's normal receive filter, which is required for `wifi_send_pkt_freedom` to inject frames reliably on NONOSDK 22x.

### Call Chain

```
wifi_send_pkt_freedom()   — libmain.a
  └─ ieee80211_freedom_output()  — libnet80211.a
```

### Channel Sync

`wifi_send_pkt_freedom` transmits on whatever channel the radio is currently tuned to. When the user changes the channel via `set_config`, the softAP is restarted on the new channel before injection resumes, keeping the config AP and the flood channel in sync.

### Single Radio

The ESP8266 has one radio. The config AP and the beacon flood always operate on the same channel. There is no way to flood channel X while accepting config connections on channel Y.

## Legal Notice

Beacon flooding disrupts the WiFi environment on the target channel for all nearby devices. **Only operate this device inside a properly RF-isolated lab.** Uncontrolled use violates FCC Part 15, Ofcom regulations, and equivalent rules in other jurisdictions.

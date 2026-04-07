# Wemos D1 Mini Lite - WiFi Multi-SSID Broadcaster

A WiFi broadcaster that rapidly switches between multiple SSIDs with a web-based configuration interface.

## Features

- **Multiple SSID Broadcasting**: Add up to 10 SSIDs that the device will broadcast
- **Rapid Switching**: Automatically switches between SSIDs every 5 seconds
- **Web Configuration**: Easy-to-use web interface for managing SSIDs
- **Persistent Storage**: Configurations saved to EEPROM
- **Station Mode Access**: Connect to your existing WiFi to access the configuration panel
- **Password Protection**: Optional password for the broadcast access points

## Hardware Requirements

- Wemos D1 Mini Lite (ESP8266)
- USB cable for programming and power

## Software Requirements

- Arduino IDE (1.8.x or newer)
- ESP8266 Board Package

### Installing ESP8266 Board Package

1. Open Arduino IDE
2. Go to **File → Preferences**
3. Add this URL to "Additional Board Manager URLs":
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
4. Go to **Tools → Board → Boards Manager**
5. Search for "esp8266" and install the package by ESP8266 Community

## Installation

1. **Clone or download** this project to your computer

2. **Open the sketch**:
   - Open `wifi_broadcaster.ino` in Arduino IDE

3. **Configure your router credentials**:
   - Edit lines 24-25 in the code:
   ```cpp
   const char* sta_ssid = "YOUR_ROUTER_SSID";
   const char* sta_password = "YOUR_ROUTER_PASSWORD";
   ```
   Replace with your actual WiFi network credentials

4. **Select the board**:
   - Go to **Tools → Board → ESP8266 Boards → LOLIN(WEMOS) D1 mini Lite**

5. **Select the port**:
   - Go to **Tools → Port** and select the port your Wemos is connected to

6. **Upload**:
   - Click the upload button (→)

## Usage

### First Time Setup

1. **Power on** the Wemos D1 Mini Lite
2. **Check Serial Monitor** (115200 baud) to see the device's IP address
3. The device will connect to your router and display its IP address

### Accessing the Web Interface

1. **Connect** your computer to the same WiFi network as the Wemos
2. **Open a web browser** and navigate to the IP address shown in Serial Monitor
   - Example: `http://192.168.1.100`

### Configuring SSIDs

1. **Add SSIDs**:
   - Enter an SSID name in the "Add New SSID" field
   - Click "Add SSID"
   - Repeat for each SSID you want to broadcast (max 10)

2. **Set Password** (Optional):
   - Enter a password in the "Access Point Password" field
   - Click "Set Password"
   - Leave empty for open networks
   - This password applies to ALL broadcast SSIDs

3. **Start Broadcasting**:
   - Click "Toggle Broadcasting" to start/stop
   - The device will cycle through all SSIDs every 5 seconds

4. **Remove SSIDs**:
   - Click the "Remove" button next to any SSID to delete it

5. **Clear All**:
   - Click "Clear All SSIDs" to remove all configured SSIDs

### Web Interface Features

- **Status Display**: Shows current broadcasting status, active SSID, and total count
- **Real-time Updates**: Interface updates every 2 seconds
- **Current SSID Highlight**: The currently broadcasting SSID is highlighted in yellow
- **Broadcasting Toggle**: Start/stop broadcasting with one click

## Configuration Options

### Changing Switch Interval

To change how often the SSIDs switch, edit line 18 in the code:

```cpp
#define SWITCH_INTERVAL 5000  // Time in milliseconds (5000 = 5 seconds)
```

For example:
- `3000` = 3 seconds
- `10000` = 10 seconds
- `30000` = 30 seconds

### Changing Maximum SSIDs

To change the maximum number of SSIDs, edit line 16:

```cpp
#define MAX_SSIDS 10  // Maximum number of SSIDs
```

**Note**: Increasing this value will use more EEPROM space.

## Troubleshooting

### Cannot Connect to Web Interface

1. Check Serial Monitor (115200 baud) for the IP address
2. Ensure your computer is on the same WiFi network
3. Try restarting the Wemos

### SSIDs Not Broadcasting

1. Make sure you've added at least one SSID
2. Click "Toggle Broadcasting" to enable
3. Check Serial Monitor for error messages

### Configuration Not Saving

1. The configuration should save automatically
2. Try power cycling the device
3. Check Serial Monitor for "Configuration saved" messages

### Device Not Connecting to WiFi

1. Verify your router credentials in the code
2. Check that your router is on and accessible
3. Ensure the WiFi network is 2.4GHz (ESP8266 doesn't support 5GHz)

## Technical Details

### How It Works

1. The device boots and connects to your existing WiFi network in **Station mode**
2. A web server runs on the Station connection for configuration
3. When broadcasting is enabled, the device switches to **AP+STA mode**
4. Every 5 seconds, it stops the current AP and starts a new one with the next SSID
5. All configurations are stored in EEPROM for persistence

### Memory Usage

- **EEPROM**: 512 bytes total
  - SSIDs: 10 × 32 bytes = 320 bytes
  - Password: 64 bytes
  - Metadata: ~8 bytes

### Network Modes

- **Station Mode (STA)**: Connected to your router - for web configuration access
- **Access Point Mode (AP)**: Broadcasting SSIDs
- **AP+STA Mode**: Both simultaneously - broadcasts SSIDs while staying connected to router

## Example Use Cases

- Testing WiFi roaming behavior
- Creating a WiFi honeypot for security research
- Simulating multiple access points
- Educational demonstrations of WiFi networks

## Security Considerations

- This device broadcasts WiFi networks - use responsibly
- Set a strong password to prevent unauthorized access
- Don't broadcast SSIDs that impersonate legitimate networks without authorization
- Use only for authorized testing and educational purposes

## License

This project is provided as-is for educational purposes.

## Credits

Built for ESP8266 (Wemos D1 Mini Lite) using Arduino framework.

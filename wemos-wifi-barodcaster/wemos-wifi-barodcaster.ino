/*
 * Wemos D1 Mini Lite - WiFi Multi-SSID Broadcaster
 *
 * Features:
 * - Connects to existing WiFi (Station mode)
 * - Hosts web server for configuration
 * - Rapidly switches between multiple broadcast SSIDs
 * - Persistent storage of SSIDs in EEPROM
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// Configuration
#define MAX_SSIDS 10
#define SSID_LENGTH 32
#define SWITCH_INTERVAL 5000  // Switch SSID every 5 seconds
#define EEPROM_SIZE 512

// Station mode credentials (change these to your router's credentials)
const char* sta_ssid = "GL-SFT12000-c18";
const char* sta_password = "goodlife";

// Web server
ESP8266WebServer server(80);

// SSID storage
struct SSIDConfig {
  char ssids[MAX_SSIDS][SSID_LENGTH];
  char password[64];
  int ssid_count;
  bool enabled;
};

SSIDConfig config;
int current_ssid_index = 0;
unsigned long last_switch_time = 0;

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n\nWemos WiFi Broadcaster Starting...");

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

  // Connect to WiFi in Station mode
  connectToWiFi();

  // Setup web server routes
  setupWebServer();

  // Start web server
  server.begin();
  Serial.println("Web server started");
  Serial.print("Access at: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();

  // Handle SSID switching if enabled
  if (config.enabled && config.ssid_count > 0) {
    unsigned long current_time = millis();

    if (current_time - last_switch_time >= SWITCH_INTERVAL) {
      switchToNextSSID();
      last_switch_time = current_time;
    }
  }
}

void connectToWiFi() {
  Serial.print("Connecting to ");
  Serial.println(sta_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(sta_ssid, sta_password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
}

void switchToNextSSID() {
  if (config.ssid_count == 0) return;

  // Stop current AP
  WiFi.softAPdisconnect(true);

  // Move to next SSID
  current_ssid_index = (current_ssid_index + 1) % config.ssid_count;

  // Start AP with new SSID
  String ssid = String(config.ssids[current_ssid_index]);

  Serial.print("Broadcasting SSID: ");
  Serial.println(ssid);

  if (strlen(config.password) > 0) {
    WiFi.softAP(ssid.c_str(), config.password);
  } else {
    WiFi.softAP(ssid.c_str());
  }

  // Set to both Station and AP mode
  WiFi.mode(WIFI_AP_STA);
}

void setupWebServer() {
  // Main page
  server.on("/", handleRoot);

  // API endpoints
  server.on("/add_ssid", HTTP_POST, handleAddSSID);
  server.on("/remove_ssid", HTTP_POST, handleRemoveSSID);
  server.on("/set_password", HTTP_POST, handleSetPassword);
  server.on("/toggle_broadcast", HTTP_POST, handleToggleBroadcast);
  server.on("/get_status", HTTP_GET, handleGetStatus);
  server.on("/clear_all", HTTP_POST, handleClearAll);

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>WiFi Broadcaster Config</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 800px;
            margin: 50px auto;
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background-color: white;
            padding: 30px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            border-bottom: 2px solid #4CAF50;
            padding-bottom: 10px;
        }
        .status {
            padding: 15px;
            margin: 20px 0;
            border-radius: 5px;
            background-color: #e3f2fd;
        }
        .status.active {
            background-color: #c8e6c9;
        }
        .ssid-list {
            list-style: none;
            padding: 0;
        }
        .ssid-item {
            background-color: #f5f5f5;
            padding: 10px;
            margin: 5px 0;
            border-radius: 5px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .ssid-item.current {
            background-color: #fff9c4;
            border: 2px solid #fbc02d;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 10px;
            margin: 5px 0;
            border: 1px solid #ddd;
            border-radius: 4px;
            box-sizing: border-box;
        }
        button {
            background-color: #4CAF50;
            color: white;
            padding: 10px 20px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            margin: 5px 5px 5px 0;
        }
        button:hover {
            background-color: #45a049;
        }
        button.danger {
            background-color: #f44336;
        }
        button.danger:hover {
            background-color: #da190b;
        }
        button.toggle {
            background-color: #2196F3;
        }
        button.toggle:hover {
            background-color: #0b7dda;
        }
        .form-group {
            margin: 20px 0;
        }
        label {
            display: block;
            margin-bottom: 5px;
            font-weight: bold;
        }
        .info {
            background-color: #fff3cd;
            padding: 10px;
            border-radius: 5px;
            margin: 10px 0;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>WiFi Broadcaster Configuration</h1>

        <div id="status" class="status">
            <strong>Status:</strong> <span id="broadcast-status">Loading...</span><br>
            <strong>Current SSID:</strong> <span id="current-ssid">-</span><br>
            <strong>Total SSIDs:</strong> <span id="total-ssids">0</span><br>
            <strong>Switch Interval:</strong> 5 seconds
        </div>

        <div class="form-group">
            <button onclick="toggleBroadcast()" class="toggle" id="toggle-btn">Toggle Broadcasting</button>
        </div>

        <h2>Broadcast SSIDs</h2>
        <div class="info">
            The broadcaster will rotate through all SSIDs below every 5 seconds
        </div>

        <ul id="ssid-list" class="ssid-list">
            <!-- SSIDs will be loaded here -->
        </ul>

        <div class="form-group">
            <label for="new-ssid">Add New SSID:</label>
            <input type="text" id="new-ssid" placeholder="Enter SSID name" maxlength="31">
            <button onclick="addSSID()">Add SSID</button>
        </div>

        <div class="form-group">
            <label for="ap-password">Access Point Password (optional):</label>
            <input type="password" id="ap-password" placeholder="Leave empty for open network" maxlength="63">
            <button onclick="setPassword()">Set Password</button>
        </div>

        <div class="form-group">
            <button onclick="clearAll()" class="danger">Clear All SSIDs</button>
        </div>
    </div>

    <script>
        function updateStatus() {
            fetch('/get_status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('broadcast-status').textContent = data.enabled ? 'Broadcasting' : 'Stopped';
                    document.getElementById('current-ssid').textContent = data.current_ssid || '-';
                    document.getElementById('total-ssids').textContent = data.ssid_count;

                    const statusDiv = document.getElementById('status');
                    if (data.enabled) {
                        statusDiv.classList.add('active');
                    } else {
                        statusDiv.classList.remove('active');
                    }

                    const ssidList = document.getElementById('ssid-list');
                    ssidList.innerHTML = '';

                    data.ssids.forEach((ssid, index) => {
                        const li = document.createElement('li');
                        li.className = 'ssid-item';
                        if (index === data.current_index) {
                            li.classList.add('current');
                        }

                        li.innerHTML = `
                            <span>${ssid}</span>
                            <button onclick="removeSSID(${index})" class="danger">Remove</button>
                        `;
                        ssidList.appendChild(li);
                    });
                })
                .catch(err => console.error('Error fetching status:', err));
        }

        function addSSID() {
            const ssid = document.getElementById('new-ssid').value.trim();
            if (!ssid) {
                alert('Please enter an SSID');
                return;
            }

            fetch('/add_ssid', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'ssid=' + encodeURIComponent(ssid)
            })
            .then(response => response.text())
            .then(data => {
                document.getElementById('new-ssid').value = '';
                updateStatus();
            })
            .catch(err => console.error('Error adding SSID:', err));
        }

        function removeSSID(index) {
            fetch('/remove_ssid', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'index=' + index
            })
            .then(() => updateStatus())
            .catch(err => console.error('Error removing SSID:', err));
        }

        function setPassword() {
            const password = document.getElementById('ap-password').value;

            fetch('/set_password', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'password=' + encodeURIComponent(password)
            })
            .then(response => response.text())
            .then(data => {
                alert('Password updated');
                document.getElementById('ap-password').value = '';
            })
            .catch(err => console.error('Error setting password:', err));
        }

        function toggleBroadcast() {
            fetch('/toggle_broadcast', {method: 'POST'})
                .then(() => updateStatus())
                .catch(err => console.error('Error toggling broadcast:', err));
        }

        function clearAll() {
            if (confirm('Are you sure you want to clear all SSIDs?')) {
                fetch('/clear_all', {method: 'POST'})
                    .then(() => updateStatus())
                    .catch(err => console.error('Error clearing SSIDs:', err));
            }
        }

        // Update status every 2 seconds
        setInterval(updateStatus, 2000);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleGetStatus() {
  String json = "{";
  json += "\"enabled\":" + String(config.enabled ? "true" : "false") + ",";
  json += "\"ssid_count\":" + String(config.ssid_count) + ",";
  json += "\"current_index\":" + String(current_ssid_index) + ",";
  json += "\"current_ssid\":\"" + (config.ssid_count > 0 ? String(config.ssids[current_ssid_index]) : "") + "\",";
  json += "\"ssids\":[";

  for (int i = 0; i < config.ssid_count; i++) {
    if (i > 0) json += ",";
    json += "\"" + String(config.ssids[i]) + "\"";
  }

  json += "]}";

  server.send(200, "application/json", json);
}

void handleAddSSID() {
  if (server.hasArg("ssid")) {
    String ssid = server.arg("ssid");

    if (config.ssid_count < MAX_SSIDS && ssid.length() > 0 && ssid.length() < SSID_LENGTH) {
      ssid.toCharArray(config.ssids[config.ssid_count], SSID_LENGTH);
      config.ssid_count++;
      saveConfig();

      server.send(200, "text/plain", "SSID added");
      Serial.println("SSID added: " + ssid);
    } else {
      server.send(400, "text/plain", "Invalid SSID or limit reached");
    }
  } else {
    server.send(400, "text/plain", "Missing SSID parameter");
  }
}

void handleRemoveSSID() {
  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();

    if (index >= 0 && index < config.ssid_count) {
      // Shift SSIDs down
      for (int i = index; i < config.ssid_count - 1; i++) {
        strcpy(config.ssids[i], config.ssids[i + 1]);
      }
      config.ssid_count--;

      // Adjust current index if needed
      if (current_ssid_index >= config.ssid_count && config.ssid_count > 0) {
        current_ssid_index = 0;
      }

      saveConfig();
      server.send(200, "text/plain", "SSID removed");
    } else {
      server.send(400, "text/plain", "Invalid index");
    }
  } else {
    server.send(400, "text/plain", "Missing index parameter");
  }
}

void handleSetPassword() {
  if (server.hasArg("password")) {
    String password = server.arg("password");

    if (password.length() < 64) {
      password.toCharArray(config.password, 64);
      saveConfig();
      server.send(200, "text/plain", "Password set");
      Serial.println("Password updated");
    } else {
      server.send(400, "text/plain", "Password too long");
    }
  } else {
    server.send(400, "text/plain", "Missing password parameter");
  }
}

void handleToggleBroadcast() {
  config.enabled = !config.enabled;

  if (config.enabled && config.ssid_count > 0) {
    current_ssid_index = 0;
    switchToNextSSID();
    Serial.println("Broadcasting enabled");
  } else {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    Serial.println("Broadcasting disabled");
  }

  saveConfig();
  server.send(200, "text/plain", config.enabled ? "Broadcasting started" : "Broadcasting stopped");
}

void handleClearAll() {
  config.ssid_count = 0;
  config.enabled = false;
  current_ssid_index = 0;

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  saveConfig();
  server.send(200, "text/plain", "All SSIDs cleared");
  Serial.println("All SSIDs cleared");
}

void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("Configuration saved");
}

void loadConfig() {
  EEPROM.get(0, config);

  // Validate loaded data
  if (config.ssid_count < 0 || config.ssid_count > MAX_SSIDS) {
    Serial.println("Invalid config, initializing defaults");
    config.ssid_count = 0;
    config.enabled = false;
    memset(config.password, 0, 64);
    saveConfig();
  } else {
    Serial.println("Configuration loaded");
    Serial.print("SSIDs: ");
    Serial.println(config.ssid_count);
  }
}

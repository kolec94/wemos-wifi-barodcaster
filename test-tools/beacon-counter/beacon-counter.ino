/*
 * Beacon Counter — test instrument for the WiFi Beacon Flooder.
 *
 * FOR USE IN A CONTROLLED / RF-ISOLATED LAB ONLY, alongside the flooder.
 *
 * Runs on a second Wemos D1 Mini. Sits in promiscuous mode on one channel and
 * counts 802.11 beacon frames, splitting them into "matching" (SSID begins with
 * TARGET_SSID) vs. "other". Prints a one-line rate report every second over
 * serial. This is a passive RECEIVER — it never transmits — so it's the
 * measurement half of the emit -> detect -> count validation loop in TESTING.md.
 *
 * Because the flooder appends 1-4 random non-printable bytes to every SSID, we
 * match on the PREFIX (the base name), not an exact string — which also confirms
 * the flooder's per-frame SSID uniqueness is working.
 *
 * Configure at runtime over serial (115200 baud). Type "help" for commands.
 */
#define REPORT_MS    1000          // rate-report interval

static const char DEFAULT_TARGET_SSID[] = "TargetSSID";
static const uint8_t DEFAULT_LISTEN_CHAN = 6;
static const uint8_t MAX_TARGET_LEN = 31;  // leaves room for the flooder's suffix

#include <ESP8266WiFi.h>
extern "C" {
  #include "user_interface.h"
}

// ---- promiscuous callback frame layout (Espressif NONOS SDK) ----
// A management frame (beacon) is delivered when len == sizeof(struct sniffer_buf2);
// its first 112 bytes sit in .buf, starting at the 802.11 header.
struct RxControl {
  signed   rssi:8;
  unsigned rate:4;
  unsigned is_group:1;
  unsigned:1;
  unsigned sig_mode:2;
  unsigned legacy_length:12;
  unsigned damatch0:1;
  unsigned damatch1:1;
  unsigned bssidmatch0:1;
  unsigned bssidmatch1:1;
  unsigned MCS:7;
  unsigned CWB:1;
  unsigned HT_length:16;
  unsigned Smoothing:1;
  unsigned Not_Sounding:1;
  unsigned:1;
  unsigned Aggregation:1;
  unsigned STBC:2;
  unsigned FEC_CODING:1;
  unsigned SGI:1;
  unsigned rxend_state:8;
  unsigned ampdu_cnt:8;
  unsigned channel:4;
  unsigned:12;
};

struct sniffer_buf2 {
  struct RxControl rx_ctrl;
  uint8_t  buf[112];
  uint16_t cnt;
  uint16_t len;
};

char targetSsid[MAX_TARGET_LEN + 1] = "TargetSSID";
uint8_t targetLen = sizeof(DEFAULT_TARGET_SSID) - 1;
uint8_t listenChannel = DEFAULT_LISTEN_CHAN;
char commandBuffer[64];
uint8_t commandLength = 0;
bool commandOverflow = false;

volatile uint32_t matchBeacons = 0;   // beacons whose SSID prefix matches targetSsid
volatile uint32_t otherBeacons = 0;   // all other beacons seen
uint32_t totalMatch = 0, totalOther = 0;
uint32_t lastReport = 0;

void IRAM_ATTR snifferCb(uint8_t *buf, uint16_t len) {
  if (len != sizeof(struct sniffer_buf2)) return;      // only full mgmt frames
  const uint8_t *fr = ((struct sniffer_buf2 *)buf)->buf;
  if (fr[0] != 0x80) return;                            // Frame Control != beacon

  // SSID is tag 0 at frame offset 36: [id=0x00][len][bytes...]
  if (fr[36] != 0x00) { otherBeacons++; return; }
  uint8_t ssidLen = fr[37];

  if (ssidLen >= targetLen && memcmp(&fr[38], targetSsid, targetLen) == 0) {
    matchBeacons++;
  } else {
    otherBeacons++;
  }
}

void printConfig() {
  Serial.printf("Config: SSID prefix '%s', channel %u\n", targetSsid, listenChannel);
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  show             Show the active configuration");
  Serial.println("  ssid <name>      Set SSID prefix (1-31 characters)");
  Serial.println("  channel <1-11>   Set and immediately tune the listen channel");
  Serial.println("  help             Show this command list");
}

void setTargetSsid(const char *value) {
  size_t length = strlen(value);
  if (length == 0) {
    Serial.println("Error: SSID must not be empty.");
    return;
  }
  if (length > MAX_TARGET_LEN) {
    Serial.println("Error: SSID must be 1-31 characters.");
    return;
  }

  wifi_promiscuous_enable(0);
  memcpy(targetSsid, value, length + 1);
  targetLen = length;
  wifi_promiscuous_enable(1);
  Serial.printf("SSID prefix set to '%s'.\n", targetSsid);
}

void setListenChannel(const char *value) {
  char *end;
  long channel = strtol(value, &end, 10);
  if (value[0] == '\0' || *end != '\0' || channel < 1 || channel > 11) {
    Serial.println("Error: channel must be a whole number from 1 to 11.");
    return;
  }

  wifi_promiscuous_enable(0);
  listenChannel = channel;
  wifi_set_channel(listenChannel);
  wifi_promiscuous_enable(1);
  Serial.printf("Listening on channel %u.\n", listenChannel);
}

void handleCommand(char *command) {
  while (*command == ' ') command++;
  char *end = command + strlen(command);
  while (end > command && end[-1] == ' ') *--end = '\0';

  if (strcmp(command, "show") == 0) {
    printConfig();
  } else if (strcmp(command, "help") == 0) {
    printHelp();
  } else if (strcmp(command, "ssid") == 0) {
    Serial.println("Error: usage is 'ssid <name>'.");
  } else if (strcmp(command, "channel") == 0) {
    Serial.println("Error: usage is 'channel <1-11>'.");
  } else if (strncmp(command, "ssid ", 5) == 0) {
    setTargetSsid(command + 5);
  } else if (strncmp(command, "channel ", 8) == 0) {
    setListenChannel(command + 8);
  } else if (command[0] != '\0') {
    Serial.println("Error: unknown command. Type 'help' for usage.");
  }
}

void readSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (commandOverflow) {
        Serial.println("Error: command is too long.");
      } else {
        commandBuffer[commandLength] = '\0';
        handleCommand(commandBuffer);
      }
      commandLength = 0;
      commandOverflow = false;
    } else if (!commandOverflow) {
      if (commandLength < sizeof(commandBuffer) - 1) {
        commandBuffer[commandLength++] = c;
      } else {
        commandOverflow = true;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nBeacon Counter starting...");
  printConfig();
  Serial.println("Type 'help' for runtime configuration commands.");
  Serial.println("cols: match/s  other/s  |  total_match  total_other");

  // Passive listener: STA mode, never associate, park in promiscuous mode.
  wifi_set_opmode(STATION_MODE);
  wifi_station_disconnect();
  wifi_set_channel(listenChannel);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(snifferCb);
  wifi_promiscuous_enable(1);
}

void loop() {
  readSerialCommands();
  uint32_t now = millis();
  if (now - lastReport >= REPORT_MS) {
    lastReport = now;
    noInterrupts();
    uint32_t m = matchBeacons; matchBeacons = 0;
    uint32_t o = otherBeacons; otherBeacons = 0;
    interrupts();
    totalMatch += m;
    totalOther += o;
    Serial.printf("%6u    %6u    |  %10u   %10u\n", m, o, totalMatch, totalOther);
  }
  yield();
}

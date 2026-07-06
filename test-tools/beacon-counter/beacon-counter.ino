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
 * Configure to mirror the flooder under test:
 */
#define TARGET_SSID  "TargetSSID"  // base SSID the flooder is broadcasting
#define LISTEN_CHAN  6             // must match the flooder's channel
#define REPORT_MS    1000          // rate-report interval

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

static const uint8_t TARGET_LEN = sizeof(TARGET_SSID) - 1;  // exclude the NUL

volatile uint32_t matchBeacons = 0;   // beacons whose SSID prefix == TARGET_SSID
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

  if (ssidLen >= TARGET_LEN && memcmp(&fr[38], TARGET_SSID, TARGET_LEN) == 0) {
    matchBeacons++;
  } else {
    otherBeacons++;
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nBeacon Counter starting...");
  Serial.printf("Matching SSID prefix '%s' on channel %d\n", TARGET_SSID, LISTEN_CHAN);
  Serial.println("cols: match/s  other/s  |  total_match  total_other");

  // Passive listener: STA mode, never associate, park in promiscuous mode.
  wifi_set_opmode(STATION_MODE);
  wifi_station_disconnect();
  wifi_set_channel(LISTEN_CHAN);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(snifferCb);
  wifi_promiscuous_enable(1);
}

void loop() {
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

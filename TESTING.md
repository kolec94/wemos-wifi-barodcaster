# Testing methodology

A repeatable, self-contained procedure for validating the beacon flooder on real
hardware — in particular the device-stability fixes from #1 (WDT survival, UI
responsiveness, AP stability, clean channel changes). It uses **two Wemos D1
Mini boards plus a laptop** as a closed emit → detect → count loop, so nothing
is ever aimed at a real network.

> Beacon flooding is a broadcast **denial-of-service** technique, not a
> machine-in-the-middle attack — nothing sits between a client and an AP. The
> laptop here is an **orchestrator/observer**, and the second D1 is a passive
> **measurement instrument**, not a victim.

## Safety & isolation (required, and also a test-validity requirement)

Run the whole rig **RF-isolated**. This is both the legal line (operating a
flooder over the air violates FCC Part 15 / Ofcom and equivalents) *and* a
measurement requirement: ambient beacons on your test channel pollute the
counts and invalidate the results.

- **Best:** a proper RF shield box / screen room.
- **Budget:** the best cheap option is *distance + a dead channel* — physically
  far from any other people/networks, on a channel a scan shows is empty. DIY
  faraday enclosures (foil-lined boxes, ammo cans, paint cans) leak badly and
  are **not** a reliable substitute for a shield box; don't trust them to
  contain the RF.
- **Always:** keep sessions short, pick an isolated channel, and scan first to
  confirm nothing else is on it.

## The rig

| Role | Hardware | Job |
|------|----------|-----|
| **DUT** (device under test) | D1 #1 — `wemos-wifi-barodcaster/` firmware | Runs the flood |
| **Instrument** | D1 #2 — `test-tools/beacon-counter/` | Passive promiscuous receiver; counts matching vs. other beacons/sec on the test channel |
| **Orchestrator** | Laptop | (a) serial console on the DUT for resets/uptime/heap; (b) WiFi client to drive the config UI; (c) *optional* Wireshark on a monitor-mode adapter as ground truth |

### Instrument setup

1. Open `test-tools/beacon-counter/beacon-counter.ino`.
2. Set `TARGET_SSID` to the flooder's base SSID and `LISTEN_CHAN` to its channel.
3. Flash to D1 #2 and open its serial monitor at 115200. It prints one line per
   second: `match/s  other/s  |  total_match  total_other`.

The counter matches on the SSID **prefix** (the base name), because the flooder
appends 1–4 random non-printable bytes per frame — so a healthy `match/s` also
confirms the per-frame SSID-uniqueness behaviour.

### Viewing both serial consoles at once

`test-tools/serial-dashboard/index.html` is a self-contained Web Serial page
(Chrome/Edge only) with two independent panels, so you can watch the DUT and
the instrument side by side instead of juggling two serial monitor windows.
Open the file directly in the browser (not hosted — it needs direct top-level
access to the ports), click **Connect** on each panel, and pick the DUT's port
and the instrument's port respectively. Each panel connects at 115200 baud with
optional per-line timestamps.

## Test matrix

Each case targets a specific fix. `match/s` refers to the instrument's output.

| # | Validates | Procedure | Pass criteria |
|---|-----------|-----------|---------------|
| T1 | WDT survival (burst loop yields) | burst = 500, flood 30+ min. The DUT prints `Beacon Flooder starting...` on every boot — tally that banner | Banner appears **once**; no `rst cause:4` (soft WDT) in the DUT serial log |
| T2 | UI responsiveness under load | During max flood, poll `GET /status` from the laptop in a loop and time each reply | Stays responsive (e.g. < 500 ms); no timeouts |
| T3 | No modem-sleep AP drops | Keep the laptop associated to `WifiBroadcaster` for the whole T1 soak | Zero disassociations |
| T4 | Emission correctness | Read the instrument during a flood | Steady non-zero `match/s`; `other/s` ≈ 0 in isolation |
| T5 | Channel-change behaviour | (a) change **SSID only** → laptop stays connected; (b) change **channel** → laptop drops once and reconnects on the new channel, instrument (re-pointed to the new channel) picks the flood up, old channel goes quiet | SSID edit = no client drop; channel edit = clean move, DUT banner tally unchanged (no crash) |
| T6 | Channel range trim | Inspect the UI channel dropdown | Only 1–11 offered (no 12/13) |
| T7 | Flash-wear no-op skip | Temporarily add `Serial.println("skip: no change");` to the early-return in `saveConfig()`, reflash, then Save an unchanged config repeatedly | Skip path fires on no-op saves; commit only happens on a real change |
| T8 | Resume-on-boot | Enable flooding, power-cycle the DUT | Comes back flooding; instrument sees `match/s` without any UI interaction |

## Metrics & how to read them

- **Reset count** — the DUT's boot banner is a free reset canary; more than one
  appearance during a soak = a reboot happened.
- **Uptime / heap** — for a longer soak, temporarily append
  ` heap=` + `ESP.getFreeHeap()` to the DUT's existing 5-second status print. A
  steady heap = no leak; a downward drift over time = investigate.
- **Instrument rate** — `match/s` is your throughput/liveness signal; a sudden
  drop to zero while `config.flooding` is true points at a stall or reset on the
  DUT.
- **UI latency** — `/status` round-trip time under load is the responsiveness
  proxy for the intra-burst yield.

## Recording a run

Capture per session: firmware commit (`git rev-parse --short HEAD`), burst size,
channel, duration, reset count, heap start/end, mean `match/s`, and any
`rst cause` lines. Keeping these lets you compare across firmware changes and
catch a regression in a later burst-loop or radio tweak.

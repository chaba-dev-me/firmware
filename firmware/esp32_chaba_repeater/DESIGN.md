# Chaba WiFi Repeater — Design Document

Firmware `0.5.8`, first of the repeater line. One sketch compiles for
both the ESP32-C3 (e.g. ESP32-C3-WROOM-02 modules) and the ESP32-C6;
the chip is detected at runtime and printed by `version`.

## What this device is

A **WiFi range extender (NAT repeater)** for the Chaba star topology:

```
                4G / Starlink uplink
                        |
                 MikroTik router
                 (CAPsMAN controller)
                        |        (2.4 GHz, star)
        +---------------+---------------+
        |                               |
   relay chaba devices           Chaba WiFi Extender
   (esp32_chaba / esp01_chaba)          |  (STA join, then AP re-broadcast)
                                phones / laptops / pumps-side kit
```

- The uplink side is a **station**: the extender joins the
  CAPsMAN-controlled SSID exactly like any client (`set wifi ssid/pass`).
- The broadcast side is a **SoftAP** on the same radio. By default it
  **mirrors the uplink** — same SSID, same password — so extended
  clients see "their" network everywhere.
- Traffic from AP clients is **NATted** onto the uplink (lwIP NAPT),
  so to the MikroTik the whole extender bubble looks like ONE client.

Both C3 and C6 are 2.4 GHz-only radios — which is what the CAPsMAN
star runs on. One radio means the AP always shares the STA's channel;
clients joined before the uplink associated see one reconnect when the
channel follows the uplink. That is physics of the chip, not a config
choice.

## Goals

- Same bench flow as every other chaba device: flash → serial CLI →
  `set wifi ssid/pass` (the CAPsMAN network) → `set id device <uuid>`
  → bootstrap registers it and fetches MQTT credentials.
- Same serial CLI grammar as the relay firmware (`help`, `show`,
  `version`, `set ...`, `boot`, `reset`, `factory`), **limited**: the
  relay commands are gone (there are no relays), and two extender
  settings are added (`set ap ssid/pass`, `status`).
- Register as a chaba device via the same bootstrap endpoint
  (migrations 039/040): `POST {device_uid, enrollment_secret}` →
  receive mqtt user/pass, customer_uid, broker host/port.
- Identify on the wire as a **wifi_router**: state payloads carry
  `"device_type": "wifi_router"` and a `router` block instead of
  `relays_state`. The MQTT bridge stores `relays_state=NULL` for it —
  the device stays visible and `last_seen` in the fleet.

## Non-goals (v1)

- **No CAPWAP/CAPsMAN client protocol.** True CAPsMAN bridging is a
  full 802.11 implementation (CAPWAP tunnel to the controller) that
  no ESP32 can run. The extender is a NAT repeater *in front of* a
  CAPsMAN network — from the controller's view it is one station.
  Consequence: clients behind the extender appear (to MikroTik
  firewall, DHCP leases, bandwidth reports) as the extender's single
  IP. Inbound connections to an extended client need a port mapping
  (`ip_portmap_add` exists in lwIP if ever needed; not exposed in v1).
- No wired backhaul, no 5 GHz (chip has none), no mesh hand-off, no
  client steering. Devices do NOT roam through it; CAPsMAN steering
  sees only the extender.
- No OTA. Same position as the relay firmware.

## File layout

```
esp32_chaba_repeater.ino   setup()/loop(), banner, NVS defaults
config.h                     pins, timing, NVS keys, topics, version
nvs_store.cpp/.h             NVS wrapper (identical to the relay firmware)
serial_cli.cpp/.h            the serial CLI (same grammar, relay cmds cut)
state.cpp/.h                 g_state: link flags, counters, dirty flag
wifi_ext.cpp/.h              THE new part: APSTA + channel follow + NAPT
mqtt_client.cpp/.h           PubSubClient wrapper (identical envelope)
bootstrap.cpp/.h             bootstrap HTTP client (verbatim from relay fw)
runtime.cpp/.h               orchestration, bootstrap cadence, LED, state pub
secrets.example.h            template; copy to secrets.h (gitignored)
```

### Compile-time defaults (secrets.h)

Same mechanism as the relay sketches. `DEFAULT_WIFI_SSID/PASS` seed
the uplink at first boot. `DEFAULT_AP_SSID/PASS` optionally pin the
broadcast SSID — blank (default) mirrors the uplink. All of it is
changeable at runtime over serial.

## NVS schema

Namespace `chaba` — the relay firmware's keys, plus:

| Key | Meaning |
|---|---|
| `wifi.ssid` / `wifi.pass` | **Uplink**: the CAPsMAN network to join |
| `ap.ssid` / `ap.pass` | **Broadcast**: blank = mirror uplink |
| `mqtt.*`, `bootstrap.*` | as in the relay firmware |
| `enroll.secret` | bootstrap credential (bench-provisioned) |
| `id.device` / `id.customer` | identity (device uid set at bench) |

NVS keys are capped at 15 chars by the NVS API.

## Serial CLI

Same parser, same masking of secrets in `show`. Relay commands are
gone; `status` and the `ap.*` settings are new. Changes to `ap.*`
apply after `reset` (the AP comes up with the runtime).

```
chaba> help
  help
  show
  status        (live: uplink, broadcast, clients, nat)
  version       (firmware version + build info)
  set wifi ssid <text>   (uplink: the network to extend)
  set wifi pass <text>
  set ap ssid <text>     (broadcast; blank = mirror uplink)
  set ap pass <text>     (broadcast; blank = mirror uplink)
  set mqtt host <host>
  set mqtt port <port>
  set mqtt user <text>  (optional; fetched via bootstrap)
  set mqtt pass <text>  (optional; fetched via bootstrap)
  set enroll secret <text> (bench: enables bootstrap)
  set bootstrap host <host>
  set bootstrap port <port>
  set id customer <uid> (optional; fetched via bootstrap)
  set id device <uid>
  boot
  reset    (reboot, keep NVS)
  factory  (wipe NVS + reboot)
```

`status` is what an installer on site actually reads:

```
--- status ---
uplink          = connected, RSSI -58 dBm, IP 10.0.4.107
broadcast       = 'FarmWifi' ch 6, IP 192.168.4.1
ap clients      = 3
nat             = on
mqtt            = connected
uptime          = 4021 s
```

## WiFi behaviour (wifi_ext.cpp)

1. Boot: `WIFI_AP_STA`; STA starts joining the uplink; SoftAP comes up
   immediately on channel 1 so the device is reachable for
   provisioning even with no uplink.
2. Uplink associates: the radio moves to the uplink's channel and the
   AP follows; `softAP` is re-issued, then NAPT is enabled on the AP
   address and the AP's DHCP "DNS server" option is pointed at the
   uplink's DNS (without this, extended clients can be offered the
   AP's own IP as resolver and DNS silently dies).
3. **Subnet collision guard**: a NAT router with the same subnet on
   both sides blackholes traffic. If the uplink hands out an address
   inside the AP's /24, the AP LAN moves down the fallback list
   (192.168.4/24 → 5/24 → 6/24) and the AP restarts on it. Logged
   loudly.
4. Uplink drop: the AP **stays up** (clients see "no internet", not
   "network gone"); the STA reconnects with the same 15 s retry as the
   relay firmware; NAPT/DNS are re-wired when the uplink returns.
5. AP client count changes are tracked (state publish trigger).

Defaults: `AP_MAX_CLIENTS 8`, AP IP 192.168.4.1/24 (softAP default).

## Status LED

Same intent as the relay device's fail-safe lamp, applied to links:

| Pattern | Meaning |
|---|---|
| steady ON | uplink + MQTT both good |
| slow blink (0.8 s) | uplink down |
| fast blink (0.2 s) | uplink up, MQTT down (bootstrap / credential trouble) |

`STATUS_LED_PIN` defaults per chip: GPIO8 (C3 Super Mini), GPIO15
(C6 Super Mini), GPIO2 (classic ESP32 DevKit, same as the relay
firmware) — override in secrets.h or config.h, `-1` disables. On the
classic ESP32, GPIO6–11 are the SPI flash: driving one of those
mid-boot corrupts flash access and bootloops the chip with a silent
`TG1WDT_SYS_RESET` (bench-verified 2026-09-29, v0.5.2) — config.h
now refuses such pins with a compile error on the classic target.

## MQTT topics and message formats

Topics identical to the relay firmware:

- cmd: `tenants/{customer_uid}/devices/{device_uid}/cmd`
- state: `tenants/{customer_uid}/devices/{device_uid}/state`
- client_id = bare device_uid (broker ACLs key on it — do not change)

### Command (broker → device)

Only `reboot` is meaningful:

```json
{"action": "reboot", "command_log_id": 123}
```

`switch_on`/`switch_off` are answered as `unknown_action` (logged,
surfaced in the next state publish as `router.last_error`) so a
misfired relay schedule aimed at this device is visible, not silent.

### State (device → broker)

Heartbeat every 60 s, plus on every observable change (debounced
200 ms): uplink up/down, AP client count, command receipt, MQTT
connect.

```json
{
  "device_type": "wifi_router",
  "rssi": -58,
  "uptime_s": 4021,
  "wifi_status": "connected",
  "mqtt_status": "connected",
  "free_heap": 181432,
  "router": {
    "uplink_ssid": "FarmWifi",
    "uplink_ip": "10.0.4.107",
    "ap_ssid": "FarmWifi",
    "ap_ip": "192.168.4.1",
    "ap_clients": 3,
    "nat": true
  }
}
```

No `relays_state` — the bridge stores NULL and the fleet still gets
`rssi`/`uptime_s`/`last_seen`. Backend device-type support
(`relay_count=0` / a `wifi_router` type in the register SP + PWA
rendering) is a deliberate follow-up; until then the device registers
like any other and simply shows no switches.

## Bootstrap / registration

Byte-identical to the relay firmware's `bootstrap.cpp`: claim-once
enrollment, 200 → store config + start MQTT; 401 → back off 10 min and
retry stored credentials; else retry in 30 s. Bench procedure:

1. Jax issues a device uid (`register /by-customer` via MCP) and an
   enrollment secret (`scripts/rotate_device_credential.py --enroll`).
2. Over serial: `set id device <uuid>`, `set enroll secret <secret>`,
   and the site's uplink: `set wifi ssid <capsman ssid>` / `set wifi
   pass <key>`.
3. `reset`. Device joins the star, bootstraps, subscribes, LED goes
   steady. Done — same as a relay box, no relay bench step.

## Board settings (Arduino IDE 2.x, esp32 core ≥ 3.x)

The sketch needs `CONFIG_LWIP_IPV4_NAPT`, which arduino-esp32 ships
enabled since the 3.x series (verified: the `ip_napt_enable` symbol is
in `liblwip.a` and links; **esp32 core 3.3.11**). On old 2.x cores the
`#if` compiles the NAT call OUT and the sketch logs a loud warning —
clients join the AP but cannot reach the uplink. Check `status`:
`nat = off` on such a core.

| Board | Select | Notes |
|---|---|---|
| ESP32-C3-WROOM-02 carrier | **ESP32C3 Dev Module** | |
| ESP32-C6 module carrier | **ESP32C6 Dev Module** | same sketch |

- Flash size: match the module (4 MB typical for WROOM-02 class).
- USB CDC On Boot: **Enabled** on native-USB carrier boards (otherwise
  the serial CLI is invisible); boards with a separate USB-UART chip
  leave it disabled.
- Verify with `version` — it prints the detected chip (C3 or C6).

## Memory / build budget

Compiled with arduino-cli, core 3.3.11:

| Target | Flash | RAM |
|---|---|---|
| esp32c3 | 1 014 913 B (77 % of 1.25 MB app) | 39 552 B (12 %) |
| esp32c6 | 1 038 928 B (79 %) | 45 548 B (13 %) |

Headroom is fine; the big consumers are WiFi + lwIP + MQTT, same as
the relay firmware.

## Libraries

Same as the relay sketches: `ArduinoJson` (7.x), `PubSubClient` (2.8).
`WiFi`/`esp_netif`/`lwip` ship with the core.

## Site deployment recommendations (bench-proven 2026-09-29)

- **Give every extender its own SSID** (`set ap ssid <site>-ext`).
  The mirror-ssid default invites sticky-roam: a client that roams
  onto the extender keeps its lease from the real AP and ends up
  "connected" with a dead gateway. Bench-verified failure mode.
- **STA power-save is off** (`WiFi.setSleep(false)`) and must stay
  off — modem sleep under an active AP causes beacon-loss disconnects
  (the 0.5.5 flap). Post-associate `softAP` re-issue is equally
  banned: reconfiguring the AP side can kick the just-associated STA
  into a connect→drop loop (the 0.5.5 flood).
- **Solid power is a requirement, not a nicety.** A weak USB port
  reboot-looped the bench board silently (connect counter pinned at
  #1 across thousands of boots). 1A+ adapter, good cable.
- **DNS: the relay is the design, not a workaround.** The esp_netif
  DHCP "DNS option" set is refused on this core
  (`ESP_ERR_ESP_NETIF_INVALID_PARAMS`), so clients always get
  192.168.4.1 as resolver and `dns_fwd` relays :53 to the uplink's
  resolver (re-learned on every reconnect — a site that changes its
  resolver heals itself). `status` shows fwd/replied counters: they
  must track 1:1.
- The classic ESP32 (D0WD) runs this sketch fine and is a valid
  bench/dev target; production remains the C3/C6 line.

## Known limitations (v1, honest list)

- Throughput: single-radio NAT repeater — expect single-digit Mbps
  (the radio halves its time between sides). Good for sensors, PWA,
  WhatsApp; not for streaming.
- Double NAT: extended clients sit behind NAT twice (MikroTik →
  extender). Outbound everything works; inbound needs port mapping.
- Same-SSID mirror means phones may "sticky-roam" onto the extender
  from far away. If a site misbehaves, give the extender its own
  SSID: `set ap ssid FarmWifi-ext` + `reset` (see the deployment
  recommendations above — this is now the recommended setup, not a
  workaround).
- `factory` wipes the enrollment secret — re-provisioning after that
  needs the bench (same as the relay firmware).

## What changes in the repo

- New sketch folder, no changes to the relay sketches. Version line
  moves to 0.5.0 for the extender only; relay firmware stays 0.4.8.
- Public repo README gains the sketch row and an extender section.

# ESP32 Firmware — Design Document

**Target hardware:** ESP32-DevKitC WROOM (Xtensa dual-core, ~320 KB RAM, 4 MB flash)
**IDE:** Arduino IDE 2.x (Espressif board support installed)
**Relay board:** 4-channel, GPIO 16/17/18/19, active-low (LOW = energised)
**Provisioning:** Serial command-line over USB at 115200 baud
**Network model:** The ESP32 connects to a local WiFi AP that has a route (or VPN) to `10.0.4.1`. The ESP32 itself does **not** run WireGuard. MQTT over plain TCP at `10.0.4.1:1883`.

## Goals

1. Join WiFi from NVS-stored credentials.
2. Connect to MQTT broker at `10.0.4.1:1883` using `PubSubClient`.
3. Subscribe to `tenants/{customer_uid}/devices/{device_uid}/cmd` and apply `switch_on`/`switch_off` commands to the 4 relays.
4. Publish device state to `tenants/{customer_uid}/devices/{device_uid}/state` on:
   - every command applied
   - relay-state changes
   - periodic heartbeat (every 60 s)
5. Survive WiFi / MQTT drops with automatic reconnect.
6. Be provisionable over USB serial without reflashing.
7. Recover the last commanded relay state after boot or reconnect: the
   backend bridge publishes `/cmd` **retained**, so the broker redelivers
   the last command on subscribe and the existing cmd handler applies it
   (no firmware logic involved).

## Non-goals (v1)

- WireGuard on the ESP32 itself (the upstream gateway handles the tunnel).
- TLS to the broker (the tunnel is the encryption layer).
- OTA updates (will be v2).
- Per-relay granularity — `switch_on` = all 4 relays on, `switch_off` = all 4 off. MCP `switch_on(device="Stub")` becomes "all relays on this device on".
- Persistent MQTT sessions (`clean_session = true`). Re-delivery of in-flight commands relies on the backend's reap-and-reclaim loop (60 s window).
- HTTP REST. All I/O is MQTT.

## File layout

```
firmware/esp32_chaba/
├── DESIGN.md            # This file
├── README.md            # Build + flash + provision instructions
├── esp32_chaba.ino    # Arduino entry point; setup() + loop()
├── config.h             # Pin map, MQTT topic templates, fallback defaults
├── secrets.h            # Local dev-only defaults (gitignored, NEVER committed)
├── secrets.example.h    # Template showing secrets.h format (committed, empty)
├── nvs_store.h/.cpp     # Preferences wrapper for NVS
├── serial_cli.h/.cpp    # Line-oriented CLI parser
├── wifi_mgr.h/.cpp      # WiFi connect + reconnect + status
├── mqtt_client.h/.cpp   # PubSubClient wrapper, subscribe, publish
├── relay_ctrl.h/.cpp    # GPIO control + state snapshot
└── state.h/.cpp         # Tiny global state struct, thread-safe reads
```

Single sketch, multiple translation units (Arduino IDE handles this automatically when all files are in the sketch folder).

### Compile-time defaults (secrets.h)

Plaintext WiFi credentials must never be committed to git, even a local-only repo. The pattern:

- `secrets.example.h` is committed, with empty values:
  ```cpp
  #define DEFAULT_WIFI_SSID ""
  #define DEFAULT_WIFI_PASS ""
  #define DEFAULT_MQTT_HOST "10.0.4.1"
  #define DEFAULT_MQTT_PORT 1883
  ```
- `secrets.h` is `.gitignore`'d. The developer creates it locally by copying `secrets.example.h` and filling in the values:
  ```cpp
  #define DEFAULT_WIFI_SSID "your-wifi-name"
  #define DEFAULT_WIFI_PASS "your-wifi-password"
  ```
- `config.h` does `#if __has_include("secrets.h") \n #include "secrets.h" \n #endif`. If `secrets.h` is missing, the empty defaults from `secrets.example.h` apply (still defined, just empty strings).

**First-boot behaviour:** if NVS has no `wifi.ssid`, the device reads `DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASS` from `secrets.h`, stores them into NVS, and proceeds to `WIFI_CONNECTING`. The developer never has to type the credentials into the serial CLI; they live in `secrets.h` and NVS.

**Rotation:** changing the password = edit `secrets.h`, reflash. NVS is wiped by `reset` CLI command if you want to re-pull from `secrets.h` on next boot.

## NVS schema

Stored via `Preferences` (which wraps ESP-IDF's NVS):

| Key | Type | Default | Notes |
|---|---|---|---|
| `wifi.ssid` | string | (empty) | Set with `set wifi ssid ...` |
| `wifi.pass` | string | (empty) | Set with `set wifi pass ...` |
| `mqtt.host` | string | `10.0.4.1` | Overridable via `set mqtt host ...` |
| `mqtt.port` | int32 | 1883 | Overridable via `set mqtt port ...` |
| `id.customer` | string | (empty) | Set with `set id customer ...` |
| `id.device` | string | (empty) | Set with `set id device ...` |
| `relay.fail_safe` | uint8 | 0 | 0 = off, 1 = on, applied when MQTT drops >60s |

If `wifi.ssid`, `id.customer`, or `id.device` is empty at boot, the device enters **CONFIG_MODE** and only the serial CLI runs. The runtime never starts.

## MQTT topics

| Direction | Topic | QoS | Retain |
|---|---|---|---|
| Subscribe (broker → device) | `tenants/{customer_uid}/devices/{device_uid}/cmd` | 1 | false |
| Publish (device → broker) | `tenants/{customer_uid}/devices/{device_uid}/state` | 1 | false |

The backend bridge worker subscribes to `tenants/+/devices/+/cmd` (publish side) and `tenants/+/devices/+/state` (subscribe side), so any device UID in any tenant UID fits cleanly.

## Message formats

### Command (broker → device)

```json
{
  "command_log_id": 5,
  "action": "switch_on",
  "payload": null,
  "issued_at": "2026-09-02 13:14:17"
}
```

For v1, only `switch_on` and `switch_off` are accepted. Unknown actions are logged and dropped (a state message is published with the command log id, status `failed`, error `unknown_action`).

### State (device → broker)

```json
{
  "command_log_id": 5,
  "relays_state": {"relay_0": "on", "relay_1": "on", "relay_2": "on", "relay_3": "on"},
  "rssi": -65,
  "uptime_s": 1234,
  "wifi_status": "connected",
  "mqtt_status": "connected",
  "free_heap": 187432
}
```

`command_log_id` is the id of the command that triggered this state, or `null` for heartbeats. `relays_state` matches the backend's `device_state.relays_state` JSON column.

## State machine

```
              ┌──────────────┐
              │     BOOT     │
              └──────┬───────┘
                     │ nvs_load()
                     ▼
            ┌───────────────────┐
            │ NVS has required? │
            └─────┬────────┬────┘
              no  │        │ yes
                  ▼        ▼
         ┌──────────────┐  ┌─────────────────┐
         │  CONFIG_MODE │  │ WIFI_CONNECTING │
         │ (CLI only)   │  └────────┬────────┘
         └──────┬───────┘           │
            boot │                  ▼
                ▼            ┌─────────────┐
        ┌──────────────┐     │ MQTT_CONN.. │ ◄──┐
        │ WIFI_CONNECT │     └──────┬──────┘    │ retry
        └──────────────┘            ▼           │ forever
                          ┌──────────────────┐  │
                          │     RUNNING      │──┘
                          └──────────────────┘
```

### CONFIG_MODE

- Serial CLI active at 115200 8N1.
- Available commands:
  - `help` — show command list.
  - `show` — print current NVS values (passwords masked).
  - `set wifi ssid <text>` / `set wifi pass <text>` — store WiFi credentials.
  - `set mqtt host <host>` / `set mqtt port <port>` — broker address.
  - `set id customer <uid>` / `set id device <uid>` — identity.
  - `set relay fail_safe <on|off>` — fail-safe state.
  - `boot` — exit config and start runtime.
  - `reset` — wipe NVS, reboot.

### RUNNING

- Three logical jobs, scheduled cooperatively in `loop()`:
  1. **WiFi tick** (every 100 ms): if disconnected, attempt reconnect. Track `wifi_last_seen_ms`.
  2. **MQTT tick** (every 100 ms): if disconnected, reconnect. On connect: subscribe cmd topic, publish current state. Call `mqttClient.loop()` to dispatch incoming.
  3. **State tick** (every 100 ms): check if state has changed (last command applied, fail-safe triggered). If so, publish.
  4. **Heartbeat tick** (every 60 s): publish state regardless.

### Fail-safe

- If `wifi_status != connected` OR `mqtt_status != connected` for >60 s, and any relay is currently ON, switch all relays OFF and publish a state with `wifi_status`/`mqtt_status` set to the relevant value.
- Behaviour is opt-out via `relay.fail_safe = 0` (default is fail-safe ON).

## Pin map

| Function | GPIO | Direction | Notes |
|---|---|---|---|
| Relay 0 | 16 | OUT | Active-low: LOW = energised |
| Relay 1 | 17 | OUT | Active-low |
| Relay 2 | 18 | OUT | Active-low |
| Relay 3 | 19 | OUT | Active-low |
| Status LED | 2 | OUT | On-board, blinks when MQTT is connected |

(Flash-strapping GPIOs 0, 2, 5, 12, 15 are avoided. GPIO 16/17/18/19 are safe on WROOM modules.)

## Memory budget

| Item | Estimate |
|---|---|
| PubSubClient buffer | 1024 B |
| ArduinoJson static doc | 512 B |
| WiFi stack | ~50 KB |
| MQTT RX buffer (PubSubClient internal) | 256 B |
| Misc globals + tasks | ~20 KB |
| **Total used** | **~75 KB / 320 KB** |

Heap is comfortable. PSRAM not used.

## Power-up defaults

- All relays OFF (relays idle state is HIGH on active-low board; `digitalWrite(pin, HIGH)` in `setup()` before configuring as OUTPUT to avoid momentary energise).
- Status LED blinks at 2 Hz while not connected to MQTT; steady on when connected.

## Libraries (install via Arduino Library Manager)

| Library | Version | Source |
|---|---|---|
| `PubSubClient` | 2.8+ | Nick O'Leary |
| `ArduinoJson` | 7.x | Benoit Blanchon |

`WiFi`, `Preferences`, and `Arduino` are built-in.

## Build steps (manual, on the dev machine)

1. File → Preferences → Additional board URLs:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Tools → Board → Boards Manager → install `esp32` (Espressif).
3. Tools → Board → `ESP32 Dev Module`.
4. Tools → Flash Size → `4MB (32Mb)`.
5. Tools → Partition Scheme → `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)`.
6. Sketch → Upload (Ctrl+U).

## Flash + provision procedure

1. Plug ESP32 into USB.
2. Open Arduino IDE, load `firmware/esp32_chaba/esp32_chaba.ino`, upload.
3. Tools → Serial Monitor, 115200 baud, **Both NL & CR**.
4. Enter:
   ```
   set wifi ssid YourSSID
   set wifi pass YourPass
   set id customer cust_fastapi_test
   set id device dev_stub_tok_41c2
   boot
   ```
5. Watch the log. After ~5 s, status LED stops blinking, "mqtt connected" prints.
6. From a host with `mosquitto_sub` pointed at the broker, confirm messages appear on `tenants/.../state`.
7. From opencode: `Turn on the Stub. My phone is 27838008753.` — status LED stays on, command applied, state JSON shows all 4 relays `on`.

## Testing strategy

**Unit-style (host-side, in `firmware/tests/`):**
- Pure-C++ tests for the JSON parse/serialise logic. Runs on the dev machine with a small harness, not on the device.

**Integration (on-device):**
- After flashing, the serial monitor log is the primary observable.
- Manual MQTT test:
  ```
  mosquitto_pub -h 10.0.4.1 -p 1883 -t tenants/cust_fastapi_test/devices/dev_stub_tok_41c2/cmd \
    -m '{"command_log_id":999,"action":"switch_on","payload":null,"issued_at":"2026-09-02 13:00:00"}'
  ```
  Expected: all 4 relays click on, status LED steady, state message published within 200 ms.

**Backend-side:**
- Existing end-to-end (MCP → backend → bridge → MQTT) stays the same. With a real ESP32 connected, the bridge's `sp_record_device_state_from_bridge` will see actual state messages and acks will land on specific commands (provided we add `command_log_id` matching — v2 procedure change).

## Open questions / v2 items

- Per-relay commands (currently `switch_on`/`switch_off` map to all-4-on/all-4-off).
- TLS to the broker, or move to MQTT-over-WireGuard on the device.
- OTA updates. Partition table needs a second `app` slot. Sketch size grows.
- Persistent MQTT sessions (`clean_session = false`) so a quick device reboot doesn't lose in-flight commands. Cost: NVS bookkeeping for pending subscriptions.
- Per-device ACL on the broker (currently anonymous + tunnel-isolation).
- WireGuard on ESP32 if the upstream router doesn't have one (current router does, so this is optional).

## What I'm NOT doing in v1

- Touching the backend, MCP, broker, or any host-side code. The backend bridge already works. Firmware talks to `10.0.4.1:1883` with anonymous MQTT over plain TCP.
- Adding a `device_status` table or any new schema.
- Changing the `command_log` lifecycle.

## What changes in the repo

- New directory `firmware/esp32_chaba/`.
- New file `firmware/esp32_chaba/DESIGN.md` (this file).
- New files per the layout above.
- New file `firmware/esp32_chaba/secrets.example.h` (committed, empty values).
- `firmware/esp32_chaba/secrets.h` is NOT committed (added to `.gitignore`).
- Top-level `.gitignore` gains `firmware/esp32_chaba/secrets.h`.
- README inside the firmware directory for build/flash instructions.
- No host-side code changes.

## Commit plan

Per project rules (local-only git, explicit commits), the work lands as 4 commits:

1. `firmware: scaffold ESP32 sketch + DESIGN.md` — empty `.ino`, `config.h`, `secrets.example.h`, design doc. Top-level `.gitignore` updated.
2. `firmware: serial CLI + NVS provisioning` — `nvs_store`, `serial_cli`, command handling in `loop()`.
3. `firmware: WiFi + MQTT + relay control + state machine` — the runtime.
4. `firmware: README with build/flash/provision steps`.

After each commit, the user reflashes from Arduino IDE (one-button upload) to verify it still builds and behaves as designed.

## Cost-of-failure modes

| Failure | Behaviour |
|---|---|
| WiFi drops | WiFi tick reconnects; relays stay in last commanded state. After 60 s, fail-safe applies (relays off). |
| MQTT drops | MQTT tick reconnects; relays stay in last commanded state. After 60 s, fail-safe applies. |
| Backend bridge drops | Backend reap loop reverts `claimed` → `issued` after 60 s. Next bridge reconnect (or this one) re-claims and re-publishes. ESP32 keeps last state. |
| ESP32 power-cycles | Boots, joins WiFi, joins MQTT, publishes current state. No commands lost (backend holds them). |
| Bad command (unknown action) | Logged, state published with `command_log_id` and an `error` field, no relay change. |
| NVS corrupt / partial | Device enters CONFIG_MODE. CLI still works, can `reset` and re-provision. |

## Stop conditions / hand-off to user

When this design lands:

- The user has a working Arduino sketch they can flash with one click.
- After flashing, the serial CLI provisions the device.
- After `boot`, the device appears in the backend's `device_state` table within a few seconds.
- An opencode `switch_on` command changes the device's relays within 500 ms.
- Pulling power on the ESP32 and re-plugging causes the device to reconnect and re-report its state.
- The user can reflash any commit and it just works.

If any of those don't hold, the design is wrong, not the implementation. Tell me which.

# Chaba Device Firmware

Arduino sketches for Chaba relay-control devices: WiFi + MQTT firmware that
joins a local network, connects to an MQTT broker, applies `switch_on` /
`switch_off` commands to relay outputs, and publishes device state. Everything
is provisionable over USB serial — no reflashing to change WiFi, broker, or
device identity.

Current firmware version: **0.4.8**.

## Sketches

| Sketch | Target | Purpose |
|--------|--------|---------|
| `firmware/esp32_chaba/` | ESP32-DevKitC WROOM (4 MB flash) | Production firmware. 4-channel relay board on GPIO 16/17/18/19 (active-low). |
| `firmware/esp01_chaba/` | ESP-01 (ESP8266, 1 MB flash) | Personal-use port of the ESP32 firmware: single relay channel on GPIO 0/2, same MQTT topics and message formats. Not for production. |
| `firmware/esp01_minimal_test/` | ESP-01 | Minimal bring-up / bisection sketch (LED + serial alive blips). Not part of the product. |

Each sketch folder contains a `DESIGN.md` with full details: hardware setup,
pin maps, serial CLI commands, board settings for the Arduino IDE, and the
message protocol.

## How it works

- WiFi credentials, MQTT broker, and device identity are stored in NVS and
  configured over a 115200-baud serial CLI (see `serial_cli.cpp`).
- The device subscribes to `tenants/{customer_uid}/devices/{device_uid}/cmd`
  and publishes state to `tenants/{customer_uid}/devices/{device_uid}/state`
  on every command, every relay-state change, and a 60 s heartbeat.
- WiFi and MQTT drops are survived with automatic reconnect; the broker
  redelivers the last retained command on resubscribe.
- Plaintext credentials are never committed: each sketch ships a
  `secrets.example.h` template. Copy it to `secrets.h` and fill in your own
  values — `secrets.h` is gitignored.

## Quick start

1. Open the sketch's `.ino` file in Arduino IDE 2.x with the Espressif
   (ESP32) or ESP8266 board support installed.
2. Copy `secrets.example.h` to `secrets.h` in the sketch folder and set your
   WiFi SSID/password and MQTT broker address.
3. Select the board settings listed in the sketch's `DESIGN.md`, then build
   and flash. **For the ESP-01, see the next section — the IDE defaults will
   not work.**
4. Open the serial monitor at 115200 baud and run `help` to see the
   provisioning CLI commands.

## ESP-01 flashing settings

The ESP-01 sketches are **very** sensitive to board settings — the Arduino
IDE defaults will boot-loop, crash with migrating exception addresses, or
fail to associate WiFi. In Arduino IDE 2.x, install the `esp8266 by ESP8266
Community` board package, select board **Generic ESP8266 Module**, and set:

| Setting | Value | Notes |
|---------|-------|-------|
| Flash mode | **DOUT** | |
| Flash size | **1MB (FS:64KB OTA:~470KB)** | IDE default 4 MB causes a boot-time Exception 0 in `ctx: sys` — the SDK places partitions past the end of the physical chip. |
| Flash frequency | **20 MHz** | At the IDE default 40 MHz the flash on many ESP-01s returns garbage reads intermittently — migrating crash sites in functions that cannot fault as written. If you see random exceptions, this is the first thing to check. |
| Crystal frequency | **26 MHz** | IDE default is 40 MHz. Many ESP-01s carry 26 MHz crystals; a wrong setting breaks RF calibration — the CPU runs at 65% speed and WiFi never associates. Self-diagnosis: firmware `Serial.begin(115200)` lands at exactly **74880 baud** on the monitor — readable garbage-free output at 74880 means the crystal setting is wrong. |
| CPU frequency | 80 MHz | |
| Reset method | ck (or dtr if your adapter supports it) | |
| Upload speed | 115200 | |

**Entering flash mode:** the ESP-01 has no auto-reset circuit. Hold
**GPIO 0 LOW** while resetting (most USB-serial adapters have a button or
jumper for this), then release. With an active-LOW relay board on GPIO 0,
holding it LOW will click the relay on during flashing — harmless and
expected. After flashing, leave GPIO 0 floating (pulled HIGH on-board) for
a normal boot.

**Power:** never power an ESP-01 from an Arduino's 3V3 pin (~50 mA vs
300+ mA radio bursts). Use a dedicated 3.3 V supply with ≥100 µF close to
the module's VCC/GND pins, common ground with the programming adapter.
Brown-outs during WiFi TX mimic every other failure mode.

**Serial monitor:** the ESP8266 boot ROM always prints at 74880 baud, the
firmware at 115200 — ~0.3 s of garbage on every reset is normal. If the
sketch doesn't come up at all, flash `firmware/esp01_minimal_test/` with
the same settings first: it's a bring-up sketch designed to bisect
power / flash / crystal problems.

Full hardware details (pin choices, GPIO 0 strap caveat, relay wiring) are
in [`firmware/esp01_chaba/DESIGN.md`](firmware/esp01_chaba/DESIGN.md).

## License

Released under the [MIT License](LICENSE).
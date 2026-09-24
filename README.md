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
   and flash.
4. Open the serial monitor at 115200 baud and run `help` to see the
   provisioning CLI commands.

## License

Released under the [MIT License](LICENSE).
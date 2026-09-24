# ESP-01 Chaba — Personal-Use Port

A stripped-down port of `esp32_chaba/` for the **ESP-01** (ESP8266, 1 MB flash,
~50 KB usable heap). Single relay channel. Same MQTT topics and message formats
as the ESP32 firmware so the backend, bridge worker, and opencode integration
work unchanged.

**Not for production.** Production stays on `esp32_chaba/` (ESP32-DevKitC WROOM
or ESP32-C3-DevKitM). This port exists because the author has three ESP-01 +
relay-board combos lying around and wants them on the bench.

## Hardware

### Board: ESP-01

- ESP8266 Tensilica L106, 80 MHz, single core
- 1 MB flash (typical) — sketch + Arduino core + libraries must fit
- ~50 KB usable heap after WiFi
- Only **GPIO 0** and **GPIO 2** are usable as I/O. GPIO 1/3 are serial.

### Pin choices

| Function | Pin | Why |
|----------|-----|-----|
| Relay IN | **GPIO 0** (default) or GPIO 2 | The ESP-01 relay boards in the bench kit drive their relay from **GPIO 0, active-LOW** (verified 2026-09-07: GND on the socket IO0 clicks the relay). Firmware default is pin 0; `set relay pin 2` in the CLI covers GPIO-2 boards/bare modules. |
| Status LED | **GPIO 2** (when relay is on GPIO 0) | ESP-01's on-board LED is wired to GPIO 2 with cathode to the pin, so it lights when the pin is LOW. Only mirrors the relay when the relay is on GPIO 2. |

**GPIO 0 strap caveat.** GPIO 0 must be HIGH at reset for a normal flash
boot. With an active-LOW relay on it, the energised state drives the pin
LOW; if the chip resets/browns out while the relay is ON, there is a
window where the strap could sample LOW and the chip lands in UART
download mode instead of booting (symptom: no banner, ROM sits waiting
for esptool; recovery: plain power-cycle). The relay board's own input
network pulls IO0 toward VCC when idle, which is why normal boots work.
Risk accepted for the bench; a 10 kΩ external pull-up GPIO0→3V3 shrinks
it further. During flashing, holding GPIO 0 LOW (as the upload procedure
requires) will click the relay on — harmless, expected.

### Relay

- Single channel, active-LOW (matches the ESP32 firmware convention)
- On the bench relay board: board 5 V input powers the ESP-01 through the
  on-board AMS1117; IN is driven from the socket's IO0 (active-LOW)
- On a bare module / GPIO-2 wiring: `set relay pin 2`, module VCC and
  JD-VCC bridged via jumper, IN1 to GPIO 2
- ESP-01's GND must be tied to the relay module's GND

### Power

ESP-01 typically powered via a 3.3 V rail on the ESP-01's onboard regulator
(or directly from USB-to-serial adapter's 3.3 V pin — most adapters have one).

**Critical:** ESP-01's onboard 3.3 V regulator is the small SOT-23 chip on the
board (AMS1117 or similar, rated 600 mA). It's fine for ESP-01 itself but NOT
fine to also power a relay coil from it. Power the relay coil from a separate
5 V supply (or, on dev boards with built-in 5 V, from that rail).

## Software differences from esp32_chaba

| Layer | ESP32 | ESP-01 (this port) |
|-------|-------|--------------------|
| Board package | esp32 by Espressif (3.3.11) | esp8266 by ESP8266 Community (3.x) |
| WiFi | `WiFi.h` | `ESP8266WiFi.h` (slightly different API but functionally equivalent for STA connect) |
| Persistent storage | ESP-IDF `nvs_flash_*` (raw) | `EEPROM` library (512-byte struct) |
| GPIO drive strength | `gpio_set_drive_capability(...)` | Not available; ESP8266 drive strength is fixed |
| JSON buffer | `StaticJsonDocument<512>` | `StaticJsonDocument<256>` (state message is small) |
| MQTT buffer | 1024 | 512 |
| Channels | 4 (GPIO 16/17/18/19) | 1 (GPIO 0 default; `set relay pin <0\|2>`) |

## MQTT topics and message formats

Subscribe to `tenants/{customer_uid}/devices/{device_uid}/cmd`,
publish state to `tenants/{customer_uid}/devices/{device_uid}/state`. See
`esp32_chaba/DESIGN.md` for the full schema.

**State wire format (this port, current).** The state payload MUST
nest the relay under `relays_state`:

```json
{
  "command_log_id": 5,
  "relays_state": {"relay_0": "on"},
  "rssi": -65,
  "uptime_s": 1234,
  "wifi_status": "connected",
  "mqtt_status": "connected",
  "free_heap": 30000
}
```

> **Historical note.** The first esp-01 port (commit `45f4a69`,
> 2026-09-03) emitted `relay_0` flat at the top level, not nested
> under `relays_state`. The bridge worker reads `relays_state` and
> silently drops `relay_0` if it's flat → `device_state.relays_state`
> ends up NULL even though commands work. Backend gained tolerance
> for the legacy flat shape at the same time the firmware was
> corrected, so deployed devices produce useful telemetry as soon
> as the backend redeploys; reflash to clear the format mismatch
> properly.

**Boot/reconnect state recovery:** the backend bridge publishes `/cmd`
retained. The broker redelivers the last command the moment this device
(re)connects and subscribes, and the existing cmd handler applies it —
the relay recovers to the last commanded state with no firmware logic.
Interplay with `relay.fail_safe on`: a >60 s disconnect forces the relay
OFF (safety), and the subsequent reconnect re-applies the last commanded
state (restoration). A device reboots briefly OFF (boot default) and
converges to the commanded state once MQTT is up.

## Provisioning

Same CLI as esp32_chaba but `relay` takes no channel argument:

```
help
show
set wifi ssid <text>
set wifi pass <text>
set mqtt host <host>
set mqtt port <port>
set id customer <uid>
set id device <uid>
set relay fail_safe <on|off>
relay on      # manual: drive GPIO 2 LOW (active-LOW relay ON)
relay off     # manual: drive GPIO 2 HIGH (relay OFF)
reset         # reboot, keep NVS
factory       # wipe EEPROM + reboot
```

`secrets.h` (gitignored) holds `DEFAULT_WIFI_SSID` and `DEFAULT_WIFI_PASS`.
On first boot with empty EEPROM, setup() loads them automatically.

## Build

Arduino IDE 2.x, board package `esp8266 by ESP8266 Community` (latest),
board setting **Generic ESP8266 Module**:

| Setting | Value |
|---------|-------|
| Flash mode | DOUT |
| Flash size | 1MB (FS:64KB OTA:~470KB) |
| **Flash frequency** | **20 MHz** — see note below |
| **Crystal frequency** | **26 MHz** — see note below |
| CPU frequency | 80 MHz |
| Reset method | ck (or dtr if your USB-serial adapter supports it) |
| Upload speed | 115200 |

**Why 26 MHz crystal frequency (2026-09-23):** the bench ESP-01s carry
26 MHz crystals; the ESP8266 core's board menu defaults to 40 MHz.
A 40 MHz build on a 26 MHz crystal is self-diagnosing: the firmware's
`Serial.begin(115200)` actually lands at 115200 × 26/40 = **exactly
74880 baud** — if readable output appears at 74880, the crystal
setting is wrong. The same wrong assumption breaks the radio (RF
calibration derives from the assumed crystal: the CPU executes at
65% speed and WiFi never associates — `wifi: timeout, retry`
forever). Found live 2026-09-23 after a settings-reset reflash showed
both symptoms at once.

**Why 20 MHz flash frequency (2026-09-07):** at the IDE default 40 MHz,
the bench ESP-01s fetched garbage instructions from flash intermittently —
migrating crash sites in functions that could not fault as written
(Exception 0 in `ram_get_fm_sar_dout`, Exception 9 in
`user_uart_wait_tx_fifo_empty`, ROM `ets_main.c` header failures). Dropping
the flash read clock to 20 MHz fixed it completely. A wrong flash-size
setting (IDE default 4 MB on a 1 MB chip) produces a related boot-time
Exception 0 in `ctx: sys` — the SDK places its system partitions past the
end of the physical chip. Always flash with the exact table above.

ESP-01 typically has only 1 MB flash and no auto-reset circuit. To flash:
hold GPIO 0 LOW at reset, release. Most USB-serial adapters have a button
or jumper for this. Once flashed, normal boot has GPIO 0 floating (HIGH
via the onboard pull-up).

## Serial monitor

The ESP8266 boot ROM always prints at **74880 baud**; the firmware prints
at 115200. On a 115200 monitor every reset begins with ~0.3 s of garbage —
that is the ROM block and is normal. Readable firmware output follows it.
Any LED wired to GPIO 2 (or any GPIO) MUST have a series resistor
(330 Ω–1 kΩ); a bare LED overcurrents the pin when driven LOW and can
reset the chip.

## Power

Never power an ESP-01 from an Arduino's 3V3 pin (~50 mA capability vs
300+ mA radio bursts). Use a dedicated 3.3 V supply (AMS1117 module or
equivalent) with ≥100 µF close to the module's VCC/GND pins, common
ground with the programming bridge.

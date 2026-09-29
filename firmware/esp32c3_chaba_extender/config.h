#pragma once

// Status LED patterns (see runtime.cpp). Defaults per chip: GPIO8 is
// the on-board LED of common ESP32-C3 Super Mini carriers, GPIO15 of
// C6 Super Minis, GPIO2 of classic ESP32 DevKits (same as the relay
// firmware). Set to -1 to disable the LED entirely.
//
// CLASSIC ESP32 WARNING: GPIO6-11 are the SPI flash chip. Driving
// those kills flash access mid-instruction — TG1WDT reset, no
// backtrace, bootloop before setup() finishes (bench-verified,
// 2026-09-29). The #if below refuses such pins at compile time.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define STATUS_LED_DEFAULT 8
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define STATUS_LED_DEFAULT 15
#else
#define STATUS_LED_DEFAULT 2  // classic ESP32
#endif

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN STATUS_LED_DEFAULT
#endif

#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32C6)
#if STATUS_LED_PIN >= 6 && STATUS_LED_PIN <= 11
#error "STATUS_LED_PIN collides with the classic ESP32 flash pins (GPIO6-11); pick another pin or -1"
#endif
#endif

#define STATUS_LED_ACTIVE_LOW_DEFAULT 1
#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32C6)
// Classic ESP32 DevKit GPIO2 LED is active-HIGH.
#undef STATUS_LED_ACTIVE_LOW_DEFAULT
#define STATUS_LED_ACTIVE_LOW_DEFAULT 0
#endif

#ifndef STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ACTIVE_LOW STATUS_LED_ACTIVE_LOW_DEFAULT
#endif

// Uplink (station) behaviour — the CAPsMAN-controlled WiFi this device
// joins as a client before re-broadcasting it.
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_TICK_MS 100

// Broadcast (SoftAP) behaviour.
#define AP_DEFAULT_CHANNEL 1
#define AP_MAX_CLIENTS 8
// Ranges tried in order when the uplink subnet would collide with the
// AP LAN (same-subnet on both sides of a NAT router silently breaks
// routing, so the AP LAN must move). All are RFC1918, 192.168.x.
static const uint8_t AP_LAN_FALLBACKS[][4] = {
    {192, 168, 4, 0},
    {192, 168, 5, 0},
    {192, 168, 6, 0},
};
#define AP_LAN_FALLBACK_COUNT (sizeof(AP_LAN_FALLBACKS) / sizeof(AP_LAN_FALLBACKS[0]))

#define MQTT_CONNECT_TIMEOUT_MS 10000
#define MQTT_TICK_MS 100
#define STATE_PUBLISH_DEBOUNCE_MS 200
#define HEARTBEAT_PERIOD_MS 60000

#define SERIAL_BAUD 115200
#define SERIAL_PROMPT "chaba> "

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif

#ifndef DEFAULT_WIFI_PASS
#define DEFAULT_WIFI_PASS ""
#endif

// Optional pinned broadcast SSID; blank = mirror the uplink.
#ifndef DEFAULT_AP_SSID
#define DEFAULT_AP_SSID ""
#endif

#ifndef DEFAULT_AP_PASS
#define DEFAULT_AP_PASS ""
#endif

#ifndef DEFAULT_MQTT_HOST
#define DEFAULT_MQTT_HOST "10.0.4.1"
#endif

#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT 1883
#endif

// Backend API the device fetches its MQTT config from on first boot
// (migration 039 bootstrap). Can be a DNS name; overridable via NVS.
#ifndef DEFAULT_BOOTSTRAP_HOST
#define DEFAULT_BOOTSTRAP_HOST "10.0.4.1"
#endif

#ifndef DEFAULT_BOOTSTRAP_PORT
#define DEFAULT_BOOTSTRAP_PORT 8000
#endif

// Bootstrap cadence. 30s between attempts keeps a failing device
// well under the backend's per-client rate limit (default 30/h).
#define BOOTSTRAP_RETRY_MS 30000
// Minimum spacing for re-bootstraps triggered by the broker REJECTING
// our credential (rc=4/5) — i.e. server-side rotation. Not an error
// loop guard for network failures (those never reach the broker).
#define BOOTSTRAP_REBOOTSTRAP_MIN_MS 300000UL
#define BOOTSTRAP_BACKOFF_401_MS 600000UL  // rejected bootstrap: 10 min

#define MQTT_KEEPALIVE_S 60
#define MQTT_QOS 1
#define MQTT_BUFFER_SIZE 1024

// NVS keys are capped at 15 characters by the NVS API — keep new keys
// within that or nvs_set_str fails at runtime, not compile time.
#define NVS_NS "chaba"
#define NVS_KEY_WIFI_SSID "wifi.ssid"
#define NVS_KEY_WIFI_PASS "wifi.pass"
#define NVS_KEY_AP_SSID "ap.ssid"
#define NVS_KEY_AP_PASS "ap.pass"
#define NVS_KEY_MQTT_HOST "mqtt.host"
#define NVS_KEY_MQTT_PORT "mqtt.port"
#define NVS_KEY_MQTT_USER "mqtt.user"
#define NVS_KEY_MQTT_PASS "mqtt.pass"
#define NVS_KEY_ENROLL_SECRET "enroll.secret"
#define NVS_KEY_BOOTSTRAP_HOST "bootstrap.host"
#define NVS_KEY_BOOTSTRAP_PORT "bootstrap.port"
#define NVS_KEY_ID_CUSTOMER "id.customer"
#define NVS_KEY_ID_DEVICE "id.device"

#define TOPIC_CMD_FMT "tenants/%s/devices/%s/cmd"
#define TOPIC_STATE_FMT "tenants/%s/devices/%s/state"

// First device of the extender line; the relay firmware stays on 0.4.x.
// 0.5.1: post-associate bring-up spread one step per tick with flushed
// forensic labels — v0.5.0 bootlooped (rst:0x8 TG1WDT) somewhere in the
// transition block and its unflushed log died with it.
// 0.5.2: the "no enroll.secret" note no longer claims bootstrap is
// impossible — claim-once (migration 040) deploys on the device uid
// alone, same as the relay firmware.
// 0.5.3: STATUS_LED_PIN default per chip — the flat GPIO8 default
// drove a FLASH data line on classic ESP32 (GPIO6-11 are the flash
// chip) and TG1WDT-bootlooped the bench board in runtime.start().
// 0.5.4: napt step asserted "Required to lock TCPIP core" — raw lwIP
// ip_napt_enable now runs under LOCK_TCPIP_CORE (bench-verified).
// 0.5.5: ap dhcp dns push failed silently on the bench (no success
// line) — every esp_netif return code is now logged, and a fallback
// DNS forwarder (dns_fwd) relays :53 to the uplink resolver when the
// push fails, so clients always get a working resolver. LED polarity
// per chip (classic ESP32 GPIO2 is active-high).
// 0.5.6: kill the uplink flap — the post-associate softAP re-issue
// (kick-loop suspect #1) is gone and STA power-save is off (beacon
// loss, suspect #2). Connect counter in the log measures any that
// remains.
// 0.5.7: forward-path instrumentation — status now prints the AP DHCP
// server state, lwIP ip forward/drop counters, and dns relay counters
// (fwd/replied/fails), so "client has no internet" decomposes into
// lease / forward / relay with two `status` calls.
#define FW_VERSION "0.5.7"

// Chaba WiFi Extender — ESP32-C3 / ESP32-C6 (WROOM-2 class modules).
//
// Joins the site's CAPsMAN-controlled 2.4 GHz network as a station,
// re-broadcasts it as a SoftAP (same SSID by default), and NATs
// downstream clients onto the uplink — a range extender for the star
// topology centred on the MikroTik router.
//
// Same serial CLI and bootstrap registration as the relay devices;
// no relay commands. Full story in DESIGN.md.

#include "config.h"
#include "nvs_store.h"
#include "serial_cli.h"
#include "state.h"
#include "wifi_ext.h"
#include "mqtt_client.h"
#include "runtime.h"

NvsStore nvs;
SerialCli cli;

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("========================================");
  Serial.print("  ESP32 Chaba Extender v");
  Serial.println(FW_VERSION);
  Serial.print("  built ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.println("========================================");

  nvs.begin();
  cli.begin(&nvs);

  if (!nvs.hasKey(NVS_KEY_WIFI_SSID) && strlen(DEFAULT_WIFI_SSID) > 0) {
    nvs.putCStr(NVS_KEY_WIFI_SSID, DEFAULT_WIFI_SSID);
    Serial.print("loaded DEFAULT_WIFI_SSID from secrets.h: ");
    Serial.println(DEFAULT_WIFI_SSID);
  }
  if (!nvs.hasKey(NVS_KEY_WIFI_PASS) && strlen(DEFAULT_WIFI_PASS) > 0) {
    nvs.putCStr(NVS_KEY_WIFI_PASS, DEFAULT_WIFI_PASS);
    Serial.println("loaded DEFAULT_WIFI_PASS from secrets.h");
  }
  if (strlen(DEFAULT_AP_SSID) > 0 && !nvs.hasKey(NVS_KEY_AP_SSID)) {
    nvs.putCStr(NVS_KEY_AP_SSID, DEFAULT_AP_SSID);
    Serial.print("loaded DEFAULT_AP_SSID from secrets.h: ");
    Serial.println(DEFAULT_AP_SSID);
  }
  if (strlen(DEFAULT_AP_PASS) > 0 && !nvs.hasKey(NVS_KEY_AP_PASS)) {
    nvs.putCStr(NVS_KEY_AP_PASS, DEFAULT_AP_PASS);
    Serial.println("loaded DEFAULT_AP_PASS from secrets.h");
  }
  if (!nvs.hasKey(NVS_KEY_MQTT_HOST) && strlen(DEFAULT_MQTT_HOST) > 0) {
    nvs.putCStr(NVS_KEY_MQTT_HOST, DEFAULT_MQTT_HOST);
    Serial.print("loaded DEFAULT_MQTT_HOST from config.h: ");
    Serial.println(DEFAULT_MQTT_HOST);
  }
  if (!nvs.hasKey(NVS_KEY_MQTT_PORT)) {
    nvs.putInt(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT);
    Serial.print("loaded DEFAULT_MQTT_PORT from config.h: ");
    Serial.println(DEFAULT_MQTT_PORT);
  }

  runtime.start();

  Serial.println("CLI ready. type 'help'");
  Serial.print(SERIAL_PROMPT);
}

void loop() {
  cli.tick();
  runtime.tick();
}

#include "config.h"
#include "nvs_store.h"
#include "serial_cli.h"
#include "state.h"
#include "relay_ctrl.h"
#include "wifi_mgr.h"
#include "mqtt_client.h"
#include "runtime.h"

NvsStore nvs;
SerialCli cli;

static int8_t resolveRelayPin() {
  int32_t p = nvs.getInt(NVS_KEY_RELAY_PIN, DEFAULT_RELAY_PIN);
  // Valid relay pins on an ESP-01: 0 or 2 (1/3 are the UART).
  if (p != 0 && p != 2) return DEFAULT_RELAY_PIN;
  return (int8_t)p;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("========================================");
  Serial.print("  ESP-01 Chaba v");
  Serial.println(FW_VERSION);
  Serial.print("  built ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.println("========================================");

  nvs.begin();
  relays.setPin(resolveRelayPin());
  relays.begin();

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

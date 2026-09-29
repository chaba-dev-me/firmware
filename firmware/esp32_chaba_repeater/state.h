#pragma once

#include <Arduino.h>

struct State {
  // Uplink (station) side
  bool wifiConnected = false;
  unsigned long wifiLastGoodMs = 0;
  // Broadcast (SoftAP) side
  bool apActive = false;
  bool naptEnabled = false;
  uint8_t apClients = 0;
  // MQTT side
  bool mqttConnected = false;
  unsigned long mqttLastGoodMs = 0;
  // Set by MqttClient when the broker rejects our credentials
  // (CONACK rc=4/5); Runtime responds by re-bootstrapping — that is
  // how server-side credential rotation reaches the device without
  // OTA.
  bool mqttAuthFailed = false;
  uint32_t lastCommandLogId = 0;
  bool lastCommandFailed = false;
  char lastError[64] = "";
  // State topic publishing: any observable change flips this flag;
  // Runtime debounces and publishes.
  bool stateDirty = true;
  unsigned long bootMs = 0;
  unsigned long lastHeartbeatMs = 0;
  unsigned long lastStatePublishMs = 0;
};

extern State g_state;

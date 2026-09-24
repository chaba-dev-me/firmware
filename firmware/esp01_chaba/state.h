#pragma once

#include <Arduino.h>

struct State {
  bool wifiConnected;
  bool mqttConnected;
  bool relayOn;
  uint32_t lastCommandLogId;
  bool lastCommandFailed;
  unsigned long bootMs;
  unsigned long wifiLastGoodMs;
  unsigned long mqttLastGoodMs;
  unsigned long lastHeartbeatMs;
  unsigned long lastStatePublishMs;
  bool failSafe;
  bool failSafeTriggered;
  // Set by MqttClient when the broker rejects our credentials
  // (CONACK rc=4/5); Runtime responds by re-bootstrapping — that
  // is how server-side credential rotation reaches the device
  // without OTA.
  bool mqttAuthFailed;
  bool relaysChanged;
  char lastError[64];
};

extern State g_state;

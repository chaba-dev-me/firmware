#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

class MqttClient {
 public:
  bool begin(const char* host, uint16_t port, const char* clientId,
             const char* username, const char* password,
             const char* cmdTopic, const char* stateTopic);
  void tick();
  bool isConnected();
  bool publishState(const char* payload);

 private:
  void onMessage(char* topic, byte* payload, unsigned int length);

  WiFiClient _wifi;
  PubSubClient _client;
  char _host[64] = "";
  char _clientId[80] = "";
  char _user[64] = "";
  char _pass[96] = "";
  char _cmdTopic[160] = "";
  char _stateTopic[160] = "";
  unsigned long _lastTickMs = 0;
  unsigned long _lastConnectAttemptMs = 0;
};

extern MqttClient mqtt;

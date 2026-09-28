#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>

// Single-radio WiFi range extender: the ESP32-C3/C6 joins the upstream
// (CAPsMAN-controlled) network as a station, re-broadcasts it as a
// SoftAP, and NATs downstream clients onto the uplink (lwIP NAPT).
//
// One radio means the AP always shares the STA's channel: clients that
// joined before the uplink associated see one reconnect when the
// channel follows the uplink. That is inherent to the chip, not a
// configuration choice.
class WifiExtender {
 public:
  // Uplink credentials come from NVS (wifi.*). AP credentials too
  // (ap.*); a blank AP SSID means "mirror the uplink SSID and pass"
  // so the extender is invisible on client side by default.
  void begin(const char* uplinkSsid, const char* uplinkPass,
             const char* apSsid, const char* apPass);
  void tick();

  bool uplinkConnected() const { return _uplinkConnected; }
  int32_t rssi() const { return _rssi; }
  const char* uplinkSsid() const { return _uplinkSsid; }
  IPAddress uplinkIp() const { return WiFi.localIP(); }
  uint8_t apClients() const { return _apClients; }
  bool naptEnabled() const { return _naptEnabled; }
  IPAddress apIp() const { return _apIp; }
  uint8_t apChannel() const { return WiFi.channel(); }
  // The SSID actually being broadcast ("mirror" resolved).
  const char* activeApSsid() const { return _activeApSsid; }

 private:
  void startAp();
  void enableNapt();
  void pushDhcpDns();
  void resolveApLanConflict();

  char _uplinkSsid[64] = "";
  char _uplinkPass[64] = "";
  char _apSsidConfig[64] = "";   // as configured ("" = mirror)
  char _apPassConfig[64] = "";
  char _activeApSsid[64] = "";

  bool _uplinkConnected = false;
  int32_t _rssi = 0;
  uint8_t _apClients = 0;
  bool _naptEnabled = false;
  bool _apStarted = false;
  IPAddress _apIp;

  bool _staConnecting = false;
  unsigned long _connectStartedMs = 0;
  unsigned long _lastTickMs = 0;
};

extern WifiExtender wifiExt;

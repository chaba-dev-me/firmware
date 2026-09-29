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
  // AP DHCP server state for `status` — the silent-killer check: a
  // client that associated but never got a 192.168.4.x lease looks
  // exactly like "no internet".
  bool apDhcpRunning();
  // The SSID actually being broadcast ("mirror" resolved).
  const char* activeApSsid() const { return _activeApSsid; }

 private:
  // Post-associate bring-up runs ONE step per tick, each bracketed by
  // flushed log lines. If the radio/driver watchdog-resets inside a
  // step, the last flushed label names the call that killed it — a
  // plain Serial.println is lost in the TX ring buffer on reset.
  enum ApStep : uint8_t {
    AP_STEP_NONE = 0,
    AP_STEP_LAN,       // resolve uplink/AP subnet collision
    AP_STEP_NAPT,      // enable NAT on the AP address
    AP_STEP_DNS,       // point ap dhcp dns at the uplink resolver
  };
  void startAp();
  void enableNapt();
  void pushDhcpDns();
  void resolveApLanConflict();
  void runApStep();
  static void logStep(const char* phase, const char* name);

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
  ApStep _apStep = AP_STEP_NONE;
  // Uplink associate/disassociate cycles since boot. A high count in
  // a short session means the uplink is flapping — the first suspect
  // for extended clients seeing "connected without internet".
  uint32_t _connectCount = 0;

  bool _staConnecting = false;
  unsigned long _connectStartedMs = 0;
  unsigned long _lastTickMs = 0;
};

extern WifiExtender wifiExt;

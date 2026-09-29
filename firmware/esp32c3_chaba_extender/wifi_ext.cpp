#include "wifi_ext.h"
#include "config.h"
#include "state.h"

#include <WiFi.h>
#include <WiFiAP.h>
#include "lwip/lwip_napt.h"
#include "lwip/tcpip.h"
#include <esp_netif.h>

WifiExtender wifiExt;

/*
Post-associate bring-up notes. Each risky call runs as its own tick
step, and the brackets are Serial.flush()ed on both sides: something
in these calls has been observed to watchdog-reset the chip
(rst:0x8 TG1WDT), and unflushed ring-buffer output dies with it.
The last flushed label is the forensic record of which call fired.
*/
void WifiExtender::logStep(const char* phase, const char* name) {
  Serial.print("wifi: step ");
  Serial.print(phase);
  Serial.print(" ");
  Serial.println(name);
  Serial.flush();
}

// One risky operation per tick. The steps only run while the loop is
// otherwise idle, so a hang inside any one of them is attributable.
void WifiExtender::runApStep() {
  switch (_apStep) {
    case AP_STEP_NONE:
      return;
    case AP_STEP_REISSUE:
      logStep("begin", "reissue-softap");
      startAp();
      logStep("done", "reissue-softap");
      _apStep = AP_STEP_LAN;
      return;
    case AP_STEP_LAN:
      logStep("begin", "lan-conflict");
      resolveApLanConflict();
      logStep("done", "lan-conflict");
      _apStep = AP_STEP_NAPT;
      return;
    case AP_STEP_NAPT:
      logStep("begin", "napt");
      enableNapt();
      logStep("done", "napt");
      _apStep = AP_STEP_DNS;
      return;
    case AP_STEP_DNS:
      logStep("begin", "dhcp-dns");
      pushDhcpDns();
      logStep("done", "dhcp-dns");
      _apStep = AP_STEP_NONE;
      g_state.stateDirty = true;
      return;
  }
}

void WifiExtender::tick() {
  unsigned long now = millis();
  if (now - _lastTickMs < WIFI_TICK_MS) return;
  _lastTickMs = now;

  runApStep();

  // ---- uplink (station) ----
  wl_status_t s = WiFi.status();
  if (s == WL_CONNECTED) {
    if (!_uplinkConnected) {
      _uplinkConnected = true;
      g_state.wifiConnected = true;
      _rssi = WiFi.RSSI();
      Serial.print("wifi: uplink connected, IP ");
      Serial.print(WiFi.localIP());
      Serial.print(", channel ");
      Serial.println(WiFi.channel());
      Serial.flush();
      // The radio just moved to the uplink's channel and the AP
      // followed. Bring NAT and DNS up ONE STEP PER TICK — see the
      // step-machine note above for why each step is bracketed.
      _apStep = AP_STEP_REISSUE;
    }
    _rssi = WiFi.RSSI();
    g_state.wifiLastGoodMs = now;
    _staConnecting = false;
  } else {
    if (_uplinkConnected) {
      Serial.print("wifi: uplink lost, status=");
      Serial.println((int)s);
      Serial.flush();
      _uplinkConnected = false;
      g_state.wifiConnected = false;
      _naptEnabled = false;  // re-enabled on reconnect
      g_state.naptEnabled = false;
      _apStep = AP_STEP_NONE;
      g_state.stateDirty = true;
    }
    if (!_staConnecting) {
      _staConnecting = true;
      _connectStartedMs = now;
      Serial.println("wifi: uplink reconnecting");
      WiFi.reconnect();
    } else if (now - _connectStartedMs > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("wifi: uplink timeout, retry");
      _connectStartedMs = now;
      WiFi.reconnect();
    }
  }

  // ---- broadcast (SoftAP) ----
  if (_apStarted) {
    _apIp = WiFi.softAPIP();
    uint8_t clients = WiFi.softAPgetStationNum();
    if (clients != _apClients) {
      Serial.print("wifi: ap clients = ");
      Serial.println(clients);
      Serial.flush();
      _apClients = clients;
      g_state.apClients = clients;
      g_state.stateDirty = true;
    }
  }
}

void WifiExtender::begin(const char* uplinkSsid, const char* uplinkPass,
                         const char* apSsid, const char* apPass) {
  strlcpy(_uplinkSsid, uplinkSsid, sizeof(_uplinkSsid));
  strlcpy(_uplinkPass, uplinkPass, sizeof(_uplinkPass));
  strlcpy(_apSsidConfig, apSsid, sizeof(_apSsidConfig));
  strlcpy(_apPassConfig, apPass, sizeof(_apPassConfig));

  // Blank AP SSID = mirror: broadcast the same network name and pass
  // as the uplink so extended clients see "their" network everywhere.
  if (strlen(_apSsidConfig) == 0) {
    strlcpy(_activeApSsid, _uplinkSsid, sizeof(_activeApSsid));
    strlcpy(_apPassConfig, _uplinkPass, sizeof(_apPassConfig));
  } else {
    strlcpy(_activeApSsid, _apSsidConfig, sizeof(_activeApSsid));
  }

  WiFi.mode(WIFI_AP_STA);

  Serial.print("wifi: uplink connecting to ");
  Serial.println(_uplinkSsid);
  WiFi.begin(_uplinkSsid, _uplinkPass);
  _staConnecting = true;
  _connectStartedMs = millis();
  g_state.wifiConnected = false;

  startAp();
}

void WifiExtender::startAp() {
  // softAP before the STA associates pins AP_DEFAULT_CHANNEL; once the
  // STA associates the radio moves and the AP follows (single radio).
  if (!WiFi.softAP(_activeApSsid, _apPassConfig, AP_DEFAULT_CHANNEL, 0,
                   AP_MAX_CLIENTS)) {
    Serial.println("wifi: softAP start FAILED (ssid/pass length?)");
    g_state.apActive = false;
    return;
  }
  _apStarted = true;
  g_state.apActive = true;
  _apIp = WiFi.softAPIP();
  Serial.print("wifi: broadcast '");
  Serial.print(_activeApSsid);
  Serial.print("' on channel ");
  Serial.print(WiFi.channel());
  Serial.print(", IP ");
  Serial.println(WiFi.softAPIP());
}

void WifiExtender::enableNapt() {
#if defined(CONFIG_LWIP_IPV4_NAPT) && CONFIG_LWIP_IPV4_NAPT
  if (_naptEnabled || !_apStarted) return;
  // Enable on the AP interface's address. The symbol only exists in
  // cores built with CONFIG_LWIP_IPV4_NAPT (arduino-esp32 >= 3.x ships
  // it enabled); the #if keeps older cores compiling — as a bridge
  // without routing, which is useless but loud in `show`.
  //
  // ip_napt_enable is RAW lwIP: it arms a sys_timeout, whose assert
  // requires the caller to hold the TCPIP core lock (bench-verified
  // 0.5.3: "assert failed: sys_timeout ... Required to lock TCPIP
  // core functionality!"). The esp_netif_* calls elsewhere in this
  // file lock internally; the raw napt calls must do it themselves.
  LOCK_TCPIP_CORE();
  ip_napt_enable(static_cast<uint32_t>(WiFi.softAPIP()), 1);
  UNLOCK_TCPIP_CORE();
  _naptEnabled = true;
  g_state.naptEnabled = true;
  Serial.println("wifi: NAT enabled on broadcast side");
#else
  Serial.println(
      "wifi: WARNING core lacks CONFIG_LWIP_IPV4_NAPT; "
      "clients will NOT reach the uplink");
#endif
}

// Point the AP's DHCP "DNS server" option at the uplink's DNS.
// Without this, extended clients can end up offered the AP's own IP
// as resolver — and there is no DNS proxy on the device.
void WifiExtender::pushDhcpDns() {
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap == nullptr) return;
  esp_netif_dns_info_t dns = {};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = static_cast<uint32_t>(WiFi.dnsIP(0));
  if (dns.ip.u_addr.ip4.addr == 0) return;
  esp_netif_dhcps_stop(ap);
  esp_err_t err = esp_netif_dhcps_option(
      ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dns, sizeof(dns));
  esp_netif_dhcps_start(ap);
  if (err == ESP_OK) {
    Serial.print("wifi: ap dhcp dns = ");
    Serial.println(WiFi.dnsIP(0));
  }
}

// A NAT router with the same subnet on both sides silently blackholes
// downstream traffic. If the uplink hands out an address inside the
// AP's /24, move the AP LAN to the next fallback range and restart
// the AP side on it.
void WifiExtender::resolveApLanConflict() {
  IPAddress apMask(255, 255, 255, 0);
  IPAddress upLinkLan = WiFi.localIP() & apMask;
  IPAddress apLan = WiFi.softAPIP() & apMask;
  if (upLinkLan != apLan) return;  // no conflict

  Serial.print("wifi: uplink LAN ");
  Serial.print(upLinkLan);
  Serial.println(" collides with ap LAN; moving ap LAN");

  for (unsigned i = 0; i < AP_LAN_FALLBACK_COUNT; i++) {
    const uint8_t* fb = AP_LAN_FALLBACKS[i];
    IPAddress cand(fb[0], fb[1], fb[2], 1);
    if ((cand & apMask) == apLan) continue;  // the colliding one
#if defined(CONFIG_LWIP_IPV4_NAPT) && CONFIG_LWIP_IPV4_NAPT
    if (_naptEnabled) {
      LOCK_TCPIP_CORE();
      ip_napt_enable(static_cast<uint32_t>(_apIp), 0);
      UNLOCK_TCPIP_CORE();
      _naptEnabled = false;
      g_state.naptEnabled = false;
    }
#endif
    WiFi.softAPConfig(cand, cand, apMask);
    startAp();
    return;
  }
  Serial.println("wifi: no free fallback LAN left; ap stays colliding");
}


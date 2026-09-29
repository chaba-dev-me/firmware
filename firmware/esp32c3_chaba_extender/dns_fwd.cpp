#include "dns_fwd.h"
#include <WiFi.h>

DnsForwarder dnsFwd;

void DnsForwarder::begin(IPAddress upstream) {
  if (_running && _upstreamDns == upstream) return;
  stop();
  if (upstream == IPAddress(0, 0, 0, 0)) return;

  // The listener must bind to the wildcard: clients address
  // 192.168.4.1 (the AP netif), while the upstream socket is routed
  // out the STA netif by the default route. Two sockets — the ESP
  // UDP API ties one local port per socket.
  if (!_listener.begin(53)) {
    Serial.println("dns_fwd: cannot bind :53 (in use?)");
    return;
  }
  // Upstream socket needs an explicit local bind for the lwIP backend.
  if (!_upstream.begin(53353)) {
    Serial.println("dns_fwd: cannot bind upstream socket");
    _listener.stop();
    return;
  }
  _upstreamDns = upstream;
  _running = true;
  Serial.print("dns_fwd: relaying :53 -> ");
  Serial.println(upstream);
}

void DnsForwarder::stop() {
  if (!_running) return;
  _listener.stop();
  _upstream.stop();
  _running = false;
  Serial.println("dns_fwd: stopped");
}

int DnsForwarder::findSlot(uint16_t txid) {
  for (uint8_t i = 0; i < MAX_INFLIGHT; i++) {
    if (_queries[i].port != 0 && _queries[i].txid == txid) return i;
  }
  return -1;
}

int DnsForwarder::freeSlot() {
  int oldest = 0;
  for (uint8_t i = 0; i < MAX_INFLIGHT; i++) {
    if (_queries[i].port == 0) return i;
    if (_queries[i].atMs < _queries[oldest].atMs) oldest = i;
  }
  return oldest;  // evict oldest
}

void DnsForwarder::tick() {
  if (!_running) return;

  // ---- AP side: a client asking 192.168.4.1:53 ----
  int p = _listener.parsePacket();
  if (p > 0) {
    uint8_t buf[512];
    int n = _listener.read(buf, sizeof(buf));
    if (n >= 12) {  // DNS header only; we relay verbatim
      uint16_t txid = (buf[0] << 8) | buf[1];
      int slot = findSlot(txid);
      if (slot < 0) slot = freeSlot();
      _queries[slot].txid = txid;
      _queries[slot].client = _listener.remoteIP();
      _queries[slot].port = _listener.remotePort();
      _queries[slot].atMs = millis();
      _upstream.beginPacket(_upstreamDns, 53);
      _upstream.write(buf, n);
      if (_upstream.endPacket() == 1) {
        _fwd++;
      } else {
        _fails++;
      }
    }
  }

  // ---- upstream side: the resolver answering ----
  p = _upstream.parsePacket();
  if (p > 0) {
    uint8_t buf[512];
    int n = _upstream.read(buf, sizeof(buf));
    if (n >= 12) {
      uint16_t txid = (buf[0] << 8) | buf[1];
      int slot = findSlot(txid);
      if (slot >= 0) {
        _listener.beginPacket(_queries[slot].client, _queries[slot].port);
        _listener.write(buf, n);
        _listener.endPacket();
        _replies++;
        _queries[slot].port = 0;  // consumed
      }
      // Unknown txid: stale/duplicate — drop.
    }
  }
}

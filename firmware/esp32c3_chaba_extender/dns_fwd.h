#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFiUdp.h>

// Minimal UDP DNS forwarder, used as a FALLBACK when the AP DHCP
// server refused to take the uplink's resolver as its "DNS server"
// option (in which case clients get 192.168.4.1 — the AP itself —
// as their resolver, and there is no DNS on the device: every phone
// then reports "no internet" because its captive check is by
// hostname). The forwarder answers queries on 0.0.0.0:53 and relays
// them to the uplink's resolver.
//
// Sized for an extender's client count: queries are mapped back to
// their asker by DNS transaction id (16 bits), 8 in flight, oldest
// evicted. Serial-scale traffic only.
class DnsForwarder {
 public:
  void begin(IPAddress upstream);
  void stop();
  void tick();
  bool running() const { return _running; }

 private:
  static const uint8_t MAX_INFLIGHT = 8;
  struct Query {
    uint16_t txid = 0;
    IPAddress client;
    uint16_t port = 0;
    uint32_t atMs = 0;
  };

  int findSlot(uint16_t txid);
  int freeSlot();

  WiFiUDP _listener;   // 0.0.0.0:53, AP side
  WiFiUDP _upstream;   // any port, routed via the STA
  IPAddress _upstreamDns;
  Query _queries[MAX_INFLIGHT];
  bool _running = false;
};

extern DnsForwarder dnsFwd;

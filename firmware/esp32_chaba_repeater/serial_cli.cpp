#include "serial_cli.h"
#include "config.h"
#include "state.h"
#include "wifi_ext.h"
#include "dns_fwd.h"
#include "lwip/stats.h"

void SerialCli::begin(NvsStore* nvs) {
  _nvs = nvs;
}

void SerialCli::tick() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (_line.length() > 0) {
        if (_echo) Serial.println();
        handleLine();
        _line = "";
      }
      printPrompt();
    } else if (c == 127 || c == 8) {
      if (_line.length() > 0) {
        _line.remove(_line.length() - 1);
        if (_echo) {
          Serial.write(8);
          Serial.write(' ');
          Serial.write(8);
        }
      }
    } else if (c >= 32 && c < 127) {
      _line += c;
      if (_echo) Serial.write(c);
    }
  }
}

void SerialCli::printPrompt() {
  Serial.print(SERIAL_PROMPT);
}

int SerialCli::tokenize(char* buf, char** argv, int maxArgs) {
  int argc = 0;
  char* tok = strtok(buf, " ");
  while (tok && argc < maxArgs) {
    argv[argc++] = tok;
    tok = strtok(NULL, " ");
  }
  return argc;
}

String SerialCli::getValue(const String& line, int argc, char** argv) {
  if (argc < 4) return "";
  int consumed = 0;
  int pos = 0;
  while (pos < (int)line.length() && consumed < 3) {
    if (line[pos] == ' ') consumed++;
    pos++;
  }
  while (pos < (int)line.length() && line[pos] == ' ') pos++;
  String v = line.substring(pos);
  v.trim();
  return v;
}

void SerialCli::handleLine() {
  String line = _line;
  line.trim();
  if (line.length() == 0) return;

  char buf[256];
  if (line.length() >= (int)sizeof(buf)) {
    Serial.println("line too long");
    return;
  }
  strncpy(buf, line.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;

  char* argv[8];
  int argc = tokenize(buf, argv, 8);

  if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
    cmd_help();
  } else if (strcmp(argv[0], "show") == 0) {
    cmd_show();
  } else if (strcmp(argv[0], "status") == 0) {
    cmd_status();
  } else if (strcmp(argv[0], "version") == 0 ||
             strcmp(argv[0], "ver") == 0) {
    cmd_version();
  } else if (strcmp(argv[0], "set") == 0) {
    cmd_set(line, argc, argv);
  } else if (strcmp(argv[0], "boot") == 0) {
    cmd_boot();
  } else if (strcmp(argv[0], "reset") == 0) {
    cmd_reset();
  } else if (strcmp(argv[0], "factory") == 0) {
    cmd_factory();
  } else {
    Serial.print("unknown command: ");
    Serial.println(argv[0]);
    Serial.println("type 'help' for a list");
  }
}

void SerialCli::cmd_version() {
  Serial.print("firmware  = ESP32 Chaba Repeater v");
  Serial.println(FW_VERSION);
  Serial.print("board     = ESP32-");
  Serial.print(ESP.getChipModel());
  Serial.print(" (");
  Serial.print(ESP.getChipCores());
  Serial.print(" core, ");
  Serial.print(getCpuFrequencyMhz());
  Serial.println(" MHz)");
  Serial.print("build     = ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.print("sdk       = ");
  Serial.print("Arduino ESP32 ");
  Serial.println(ESP.getSdkVersion());
}

void SerialCli::cmd_help() {
  Serial.println("commands:");
  Serial.println("  help");
  Serial.println("  show");
  Serial.println("  status        (live: uplink, broadcast, clients, nat)");
  Serial.println("  version       (firmware version + build info)");
  Serial.println("  set wifi ssid <text>   (uplink: the network to extend)");
  Serial.println("  set wifi pass <text>");
  Serial.println("  set ap ssid <text>     (broadcast; blank = mirror uplink)");
  Serial.println("  set ap pass <text>     (broadcast; blank = mirror uplink)");
  Serial.println("  set mqtt host <host>");
  Serial.println("  set mqtt port <port>");
  Serial.println("  set mqtt user <text>  (optional; fetched via bootstrap)");
  Serial.println("  set mqtt pass <text>  (optional; fetched via bootstrap)");
  Serial.println("  set enroll secret <text> (bench: enables bootstrap)");
  Serial.println("  set bootstrap host <host>");
  Serial.println("  set bootstrap port <port>");
  Serial.println("  set id customer <uid> (optional; fetched via bootstrap)");
  Serial.println("  set id device <uid>");
  Serial.println("  boot");
  Serial.println("  reset    (reboot, keep NVS)");
  Serial.println("  factory  (wipe NVS + reboot)");
}

static void printMasked(NvsStore* nvs, const char* key) {
  char buf[64];
  int len = 0;
  if (nvs->getCStr(key, buf, sizeof(buf))) {
    len = strlen(buf);
  }
  if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
  for (int i = 0; i < len; i++) buf[i] = '*';
  buf[len] = 0;
  Serial.println(buf);
}

void SerialCli::cmd_show() {
  char buf[64];
  Serial.println();
  Serial.println("--- configuration ---");

  Serial.print("firmware        = ESP32 Chaba Repeater v");
  Serial.println(FW_VERSION);

  Serial.print("wifi.ssid       = ");
  if (!_nvs->getCStr(NVS_KEY_WIFI_SSID, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("wifi.pass       = ");
  printMasked(_nvs, NVS_KEY_WIFI_PASS);

  Serial.print("ap.ssid         = ");
  if (!_nvs->getCStr(NVS_KEY_AP_SSID, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(strlen(buf) ? buf : "(mirror uplink)");

  Serial.print("ap.pass         = ");
  printMasked(_nvs, NVS_KEY_AP_PASS);

  Serial.print("mqtt.host       = ");
  if (!_nvs->getCStr(NVS_KEY_MQTT_HOST, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("mqtt.port       = ");
  Serial.println(_nvs->getInt(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT));

  Serial.print("mqtt.user       = ");
  if (!_nvs->getCStr(NVS_KEY_MQTT_USER, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("mqtt.pass       = ");
  printMasked(_nvs, NVS_KEY_MQTT_PASS);

  Serial.print("enroll.secret   = ");
  printMasked(_nvs, NVS_KEY_ENROLL_SECRET);

  Serial.print("bootstrap.host  = ");
  if (!_nvs->getCStr(NVS_KEY_BOOTSTRAP_HOST, buf, sizeof(buf)) || buf[0] == 0) {
    snprintf(buf, sizeof(buf), "%s", DEFAULT_BOOTSTRAP_HOST);
  }
  Serial.println(buf);

  Serial.print("bootstrap.port  = ");
  int32_t bpShow = _nvs->getInt(NVS_KEY_BOOTSTRAP_PORT, DEFAULT_BOOTSTRAP_PORT);
  if (bpShow <= 0 || bpShow > 65535) bpShow = DEFAULT_BOOTSTRAP_PORT;
  Serial.println(bpShow);

  Serial.print("id.customer     = ");
  if (!_nvs->getCStr(NVS_KEY_ID_CUSTOMER, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("id.device       = ");
  if (!_nvs->getCStr(NVS_KEY_ID_DEVICE, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  cmd_status();
}

// Live link status — the part an installer on site actually needs:
// is the uplink up, what is being broadcast, who is on it, does NAT
// route to the uplink.
void SerialCli::cmd_status() {
  Serial.println("--- status ---");
  Serial.print("uplink          = ");
  if (wifiExt.uplinkConnected()) {
    Serial.print("connected, RSSI ");
    Serial.print(wifiExt.rssi());
    Serial.print(" dBm, IP ");
    Serial.println(wifiExt.uplinkIp());
  } else {
    Serial.println("down");
  }
  Serial.print("broadcast       = ");
  if (g_state.apActive) {
    Serial.print("'");
    Serial.print(wifiExt.activeApSsid());
    Serial.print("' ch ");
    Serial.print(wifiExt.apChannel());
    Serial.print(", IP ");
    Serial.println(wifiExt.apIp());
  } else {
    Serial.println("off");
  }
  Serial.print("ap clients      = ");
  Serial.println(wifiExt.apClients());
  Serial.print("nat             = ");
  Serial.println(wifiExt.naptEnabled() ? "on" : "off");
  Serial.print("ap dhcp server  = ");
  Serial.println(wifiExt.apDhcpRunning() ? "running" : "STOPPED");
#if LWIP_STATS
  // The forward-path verdict: with a client attempting traffic, fw
  // moving means packets ARE being forwarded (problem is return/NAPT);
  // fw stuck at 0 means they never reach the forward path.
  Serial.print("ip fwd/drop     = ");
  Serial.print(lwip_stats.ip.fw);
  Serial.print(" / ");
  Serial.println(lwip_stats.ip.drop);
#endif
  Serial.print("dns relay       = ");
  if (dnsFwd.running()) {
    Serial.print("on, fwd ");
    Serial.print(dnsFwd.queriesForwarded());
    Serial.print(", replied ");
    Serial.print(dnsFwd.repliesRelayed());
    Serial.print(", send-fails ");
    Serial.println(dnsFwd.sendFailures());
  } else {
    Serial.println("off (dhcp dns option took)");
  }
  Serial.print("mqtt            = ");
  Serial.println(g_state.mqttConnected ? "connected" : "disconnected");
  Serial.print("uptime          = ");
  Serial.print((millis() - g_state.bootMs) / 1000);
  Serial.println(" s");
  Serial.println();
}

void SerialCli::cmd_boot() {
  Serial.println("boot: runtime auto-starts in setup() if config is complete");
  Serial.println("boot: this command is reserved for a future manual restart");
}

void SerialCli::cmd_reset() {
  Serial.println("rebooting");
  delay(300);
  ESP.restart();
}

void SerialCli::cmd_factory() {
  Serial.println("wiping NVS...");
  _nvs->wipe();
  Serial.println("OK; rebooting");
  delay(500);
  ESP.restart();
}

void SerialCli::cmd_set(const String& line, int argc, char** argv) {
  if (argc < 4) {
    Serial.println("usage: set <section> <key> <value>");
    return;
  }
  const char* section = argv[1];
  const char* key = argv[2];
  String value = getValue(line, argc, argv);

  if (strcmp(section, "wifi") == 0 && strcmp(key, "ssid") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_WIFI_SSID, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "wifi") == 0 && strcmp(key, "pass") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_WIFI_PASS, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "ap") == 0 && strcmp(key, "ssid") == 0) {
    // Blank value clears the override: broadcast mirrors the uplink.
    Serial.println(_nvs->putCStr(NVS_KEY_AP_SSID, value.c_str()) ? "OK; applies after reset" : "ERROR: not stored");
  } else if (strcmp(section, "ap") == 0 && strcmp(key, "pass") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_AP_PASS, value.c_str()) ? "OK; applies after reset" : "ERROR: not stored");
  } else if (strcmp(section, "mqtt") == 0 && strcmp(key, "host") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_MQTT_HOST, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "mqtt") == 0 && strcmp(key, "port") == 0) {
    int32_t port = value.toInt();
    if (port <= 0 || port > 65535) {
      Serial.println("invalid port");
      return;
    }
    _nvs->putInt(NVS_KEY_MQTT_PORT, port);
    Serial.println("OK");
  } else if (strcmp(section, "mqtt") == 0 && strcmp(key, "user") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_MQTT_USER, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "mqtt") == 0 && strcmp(key, "pass") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_MQTT_PASS, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "enroll") == 0 && strcmp(key, "secret") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_ENROLL_SECRET, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "bootstrap") == 0 && strcmp(key, "host") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_BOOTSTRAP_HOST, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "bootstrap") == 0 && strcmp(key, "port") == 0) {
    int32_t port = value.toInt();
    if (port <= 0 || port > 65535) {
      Serial.println("invalid port");
      return;
    }
    _nvs->putInt(NVS_KEY_BOOTSTRAP_PORT, port);
    Serial.println("OK");
  } else if (strcmp(section, "id") == 0 && strcmp(key, "customer") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_ID_CUSTOMER, value.c_str()) ? "OK" : "ERROR: not stored");
  } else if (strcmp(section, "id") == 0 && strcmp(key, "device") == 0) {
    Serial.println(_nvs->putCStr(NVS_KEY_ID_DEVICE, value.c_str()) ? "OK" : "ERROR: not stored");
  } else {
    Serial.print("unknown setting: ");
    Serial.print(section);
    Serial.print(".");
    Serial.println(key);
  }
}

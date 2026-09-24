#include "serial_cli.h"
#include "config.h"
#include "relay_ctrl.h"
#include "state.h"

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
  } else if (strcmp(argv[0], "version") == 0 ||
             strcmp(argv[0], "ver") == 0) {
    cmd_version();
  } else if (strcmp(argv[0], "set") == 0) {
    cmd_set(line, argc, argv);
  } else if (strcmp(argv[0], "relay") == 0) {
    cmd_relay(argc, argv);
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
  Serial.print("firmware  = ESP32 Chaba v");
  Serial.println(FW_VERSION);
  Serial.print("board     = ESP32 (");
  Serial.print(F_CPU / 1000000);
  Serial.println(" MHz)");
  Serial.print("build     = ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.print("sdk       = ");
  #ifdef ESP32
    Serial.print("Arduino ESP32 ");
    Serial.println(ESP.getSdkVersion());
  #else
    Serial.println("(unknown)");
  #endif
}

void SerialCli::cmd_help() {
  Serial.println("commands:");
  Serial.println("  help");
  Serial.println("  show");
  Serial.println("  version       (firmware version + build info)");
  Serial.println("  set wifi ssid <text>");
  Serial.println("  set wifi pass <text>");
  Serial.println("  set mqtt host <host>");
  Serial.println("  set mqtt port <port>");
  Serial.println("  set mqtt user <text>  (optional; fetched via bootstrap)");
  Serial.println("  set mqtt pass <text>  (optional; fetched via bootstrap)");
  Serial.println("  set enroll secret <text> (bench: enables bootstrap)");
  Serial.println("  set bootstrap host <host>");
  Serial.println("  set bootstrap port <port>");
  Serial.println("  set id customer <uid> (optional; fetched via bootstrap)");
  Serial.println("  set id device <uid>");
  Serial.println("  set relay fail_safe <on|off>");
  Serial.println("  relay <ch> <on|off>    (manual: ch = 0..3, toggles GPIO directly)");
  Serial.println("  boot");
  Serial.println("  reset    (reboot, keep NVS)");
  Serial.println("  factory  (wipe NVS + reboot)");
}

void SerialCli::cmd_show() {
  char buf[64];
  Serial.println();
  Serial.println("--- configuration ---");

  Serial.print("firmware        = ESP32 Chaba v");
  Serial.println(FW_VERSION);

  Serial.print("wifi.ssid       = ");
  if (!_nvs->getCStr(NVS_KEY_WIFI_SSID, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("wifi.pass       = ");
  int passLen = 0;
  if (_nvs->getCStr(NVS_KEY_WIFI_PASS, buf, sizeof(buf))) {
    passLen = strlen(buf);
  }
  if (passLen > (int)sizeof(buf) - 1) passLen = sizeof(buf) - 1;
  for (int i = 0; i < passLen; i++) buf[i] = '*';
  buf[passLen] = 0;
  Serial.println(buf);

  Serial.print("mqtt.host       = ");
  if (!_nvs->getCStr(NVS_KEY_MQTT_HOST, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("mqtt.port       = ");
  Serial.println(_nvs->getInt(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT));

  Serial.print("mqtt.user       = ");
  if (!_nvs->getCStr(NVS_KEY_MQTT_USER, buf, sizeof(buf))) buf[0] = 0;
  Serial.println(buf);

  Serial.print("mqtt.pass       = ");
  int mqttPassLen = 0;
  if (_nvs->getCStr(NVS_KEY_MQTT_PASS, buf, sizeof(buf))) {
    mqttPassLen = strlen(buf);
  }
  if (mqttPassLen > (int)sizeof(buf) - 1) mqttPassLen = sizeof(buf) - 1;
  for (int i = 0; i < mqttPassLen; i++) buf[i] = '*';
  buf[mqttPassLen] = 0;
  Serial.println(buf);

  Serial.print("enroll.secret   = ");
  int enrollLen = 0;
  if (_nvs->getCStr(NVS_KEY_ENROLL_SECRET, buf, sizeof(buf))) {
    enrollLen = strlen(buf);
  }
  if (enrollLen > (int)sizeof(buf) - 1) enrollLen = sizeof(buf) - 1;
  for (int i = 0; i < enrollLen; i++) buf[i] = '*';
  buf[enrollLen] = 0;
  Serial.println(buf);

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

  Serial.print("relay.fail_safe = ");
  Serial.println(_nvs->getInt(NVS_KEY_FAIL_SAFE) ? "on" : "off");
  Serial.println();
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
  } else if (strcmp(section, "relay") == 0 && strcmp(key, "fail_safe") == 0) {
    int v;
    if (value == "on") v = 1;
    else if (value == "off") v = 0;
    else {
      Serial.println("value must be 'on' or 'off'");
      return;
    }
    _nvs->putInt(NVS_KEY_FAIL_SAFE, v);
    Serial.println("OK");
  } else {
    Serial.print("unknown setting: ");
    Serial.print(section);
    Serial.print(".");
    Serial.println(key);
  }
}

void SerialCli::cmd_relay(int argc, char** argv) {
  if (argc < 3) {
    Serial.println("usage: relay <ch> <on|off>     (ch = 0..3)");
    Serial.println("chips: 0=GPIO16  1=GPIO17  2=GPIO18  3=GPIO19");
    Serial.println("manual control bypasses MQTT. Use this to verify wiring.");
    return;
  }
  int ch = atoi(argv[1]);
  if (ch < 0 || ch >= 4) {
    Serial.println("ch must be 0..3");
    return;
  }
  bool on = (strcmp(argv[2], "on") == 0);
  if (strcmp(argv[2], "on") != 0 && strcmp(argv[2], "off") != 0) {
    Serial.println("value must be 'on' or 'off'");
    return;
  }
  relays.setChannel(ch, on);
  Serial.print("relay ");
  Serial.print(ch);
  Serial.print(" (GPIO");
  const int pins[4] = {16, 17, 18, 19};
  Serial.print(pins[ch]);
  Serial.print(") -> ");
#if RELAY_ACTIVE_LOW
  Serial.print(on ? "ON (drive LOW)" : "OFF (drive HIGH)");
#else
  Serial.print(on ? "ON (drive HIGH)" : "OFF (drive LOW)");
#endif
  Serial.print("  level=");
  Serial.println(digitalRead(pins[ch]) ? "HIGH" : "LOW");
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

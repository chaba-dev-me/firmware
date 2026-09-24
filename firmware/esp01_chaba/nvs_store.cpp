#include "nvs_store.h"
#include "config.h"
#include <EEPROM.h>

// Magic version 0x...03: mqtt_user/mqtt_pass/enroll_secret/bootstrap_host/
// bootstrap_port appended (0.4.6 — the bootstrap-port keys existed in
// config.h but had NO struct slots, so every write silently failed and
// the first ESP-01 claim lost its credential; found live 2026-09-23).
// Fields are APPENDED, so old offsets stay valid, but unwritten EEPROM
// reads 0xFF (not zeros) so the whole layout re-inits via magic bump —
// one-time re-entry of wifi/id on affected chips.
// Version 0x...02 was: id_customer/id_device widened 32->40 bytes.
#define NVS_MAGIC 0xA55A0103UL
#define NVS_SIZE  1024

struct NvsData {
  uint32_t magic;
  char wifi_ssid[32];
  char wifi_pass[64];
  char mqtt_host[64];
  int32_t mqtt_port;
  char id_customer[40];
  char id_device[40];
  int32_t relay_fail_safe;
  int32_t relay_pin;
  // 0x...03 additions (appended: pre-0x...03 offsets unchanged)
  char mqtt_user[64];
  char mqtt_pass[96];     // token_urlsafe(32) = 43 chars
  char enroll_secret[96];
  char bootstrap_host[64];
  int32_t bootstrap_port;
};

void NvsStore::begin() {
  EEPROM.begin(NVS_SIZE);
  uint32_t magic;
  EEPROM.get(0, magic);
  if (magic != NVS_MAGIC) {
    NvsData blank;
    memset(&blank, 0, sizeof(blank));
    blank.magic = NVS_MAGIC;
    blank.relay_pin = -1;  // -1 = unset, use DEFAULT_RELAY_PIN
    EEPROM.put(0, blank);
    EEPROM.commit();
  }
  _ok = true;
}

int NvsStore::_offsetFor(const char* key) const {
  if (strcmp(key, NVS_KEY_WIFI_SSID) == 0)    return offsetof(NvsData, wifi_ssid);
  if (strcmp(key, NVS_KEY_WIFI_PASS) == 0)    return offsetof(NvsData, wifi_pass);
  if (strcmp(key, NVS_KEY_MQTT_HOST) == 0)    return offsetof(NvsData, mqtt_host);
  if (strcmp(key, NVS_KEY_MQTT_PORT) == 0)    return offsetof(NvsData, mqtt_port);
  if (strcmp(key, NVS_KEY_ID_CUSTOMER) == 0)  return offsetof(NvsData, id_customer);
  if (strcmp(key, NVS_KEY_ID_DEVICE) == 0)    return offsetof(NvsData, id_device);
  if (strcmp(key, NVS_KEY_FAIL_SAFE) == 0)    return offsetof(NvsData, relay_fail_safe);
  if (strcmp(key, NVS_KEY_RELAY_PIN) == 0)    return offsetof(NvsData, relay_pin);
  if (strcmp(key, NVS_KEY_MQTT_USER) == 0)    return offsetof(NvsData, mqtt_user);
  if (strcmp(key, NVS_KEY_MQTT_PASS) == 0)    return offsetof(NvsData, mqtt_pass);
  if (strcmp(key, NVS_KEY_ENROLL_SECRET) == 0) return offsetof(NvsData, enroll_secret);
  if (strcmp(key, NVS_KEY_BOOTSTRAP_HOST) == 0) return offsetof(NvsData, bootstrap_host);
  if (strcmp(key, NVS_KEY_BOOTSTRAP_PORT) == 0) return offsetof(NvsData, bootstrap_port);
  return -1;
}

int NvsStore::_maxLenFor(const char* key) const {
  if (strcmp(key, NVS_KEY_WIFI_SSID) == 0)    return sizeof(((NvsData*)0)->wifi_ssid);
  if (strcmp(key, NVS_KEY_WIFI_PASS) == 0)    return sizeof(((NvsData*)0)->wifi_pass);
  if (strcmp(key, NVS_KEY_MQTT_HOST) == 0)    return sizeof(((NvsData*)0)->mqtt_host);
  if (strcmp(key, NVS_KEY_ID_CUSTOMER) == 0)  return sizeof(((NvsData*)0)->id_customer);
  if (strcmp(key, NVS_KEY_ID_DEVICE) == 0)    return sizeof(((NvsData*)0)->id_device);
  if (strcmp(key, NVS_KEY_MQTT_USER) == 0)    return sizeof(((NvsData*)0)->mqtt_user);
  if (strcmp(key, NVS_KEY_MQTT_PASS) == 0)    return sizeof(((NvsData*)0)->mqtt_pass);
  if (strcmp(key, NVS_KEY_ENROLL_SECRET) == 0) return sizeof(((NvsData*)0)->enroll_secret);
  if (strcmp(key, NVS_KEY_BOOTSTRAP_HOST) == 0) return sizeof(((NvsData*)0)->bootstrap_host);
  return -1;
}

bool NvsStore::_isInt(const char* key) const {
  return strcmp(key, NVS_KEY_MQTT_PORT) == 0
      || strcmp(key, NVS_KEY_FAIL_SAFE) == 0
      || strcmp(key, NVS_KEY_RELAY_PIN) == 0
      || strcmp(key, NVS_KEY_BOOTSTRAP_PORT) == 0;
}

bool NvsStore::getCStr(const char* key, char* buf, size_t bufLen) {
  if (!_ok || buf == NULL || bufLen == 0) return false;
  if (_isInt(key)) return false;
  int off = _offsetFor(key);
  int maxLen = _maxLenFor(key);
  if (off < 0 || maxLen <= 0) return false;

  char tmp[96];
  if ((size_t)maxLen > sizeof(tmp)) maxLen = sizeof(tmp);
  for (int i = 0; i < maxLen; i++) tmp[i] = EEPROM.read(off + i);

  size_t copyLen = strnlen(tmp, maxLen);
  if (copyLen == 0) {
    buf[0] = 0;
    return false;
  }
  if (copyLen >= bufLen) copyLen = bufLen - 1;
  memcpy(buf, tmp, copyLen);
  buf[copyLen] = 0;
  return true;
}

bool NvsStore::putCStr(const char* key, const char* value) {
  if (!_ok || value == NULL) return false;
  if (_isInt(key)) return false;
  int off = _offsetFor(key);
  int maxLen = _maxLenFor(key);
  if (off < 0 || maxLen <= 0) return false;

  size_t valLen = strlen(value);
  if ((size_t)maxLen > 96) maxLen = 96;
  if (valLen >= (size_t)maxLen) valLen = maxLen - 1;

  for (int i = 0; i < maxLen; i++) {
    char c = (i < (int)valLen) ? value[i] : 0;
    EEPROM.write(off + i, c);
  }
  return EEPROM.commit();
}

int32_t NvsStore::getInt(const char* key, int32_t defaultValue) {
  if (!_ok) return defaultValue;
  if (!_isInt(key)) return defaultValue;
  int off = _offsetFor(key);
  if (off < 0) return defaultValue;
  int32_t v;
  EEPROM.get(off, v);
  return v;
}

bool NvsStore::putInt(const char* key, int32_t value) {
  if (!_ok) return false;
  if (!_isInt(key)) return false;
  int off = _offsetFor(key);
  if (off < 0) return false;
  EEPROM.put(off, value);
  return EEPROM.commit();
}

bool NvsStore::hasKey(const char* key) {
  if (!_ok) return false;
  if (_isInt(key)) {
    int off = _offsetFor(key);
    if (off < 0) return false;
    int32_t v;
    EEPROM.get(off, v);
    return v != 0;
  }
  char buf[96];
  return getCStr(key, buf, sizeof(buf));
}

bool NvsStore::wipe() {
  if (!_ok) return false;
  for (int i = 0; i < NVS_SIZE; i++) EEPROM.write(i, 0xFF);
  return EEPROM.commit();
}

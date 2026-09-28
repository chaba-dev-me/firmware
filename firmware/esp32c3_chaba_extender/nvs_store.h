#pragma once

#include <Arduino.h>

class NvsStore {
 public:
  void begin();
  bool getCStr(const char* key, char* buf, size_t bufLen);
  bool putCStr(const char* key, const char* value);
  int32_t getInt(const char* key, int32_t defaultValue = 0);
  bool putInt(const char* key, int32_t value);
  bool hasKey(const char* key);
  bool wipe();

 private:
  uint32_t _handle = 0;  // nvs_handle_t
  bool _ok = false;
};

#pragma once

#include <Arduino.h>

class NvsStore {
public:
  void begin();
  bool getCStr(const char* key, char* buf, size_t bufLen);
  bool putCStr(const char* key, const char* value);
  int32_t getInt(const char* key, int32_t defaultValue);
  bool putInt(const char* key, int32_t value);
  bool hasKey(const char* key);
  bool wipe();

private:
  bool _ok = false;

  int _offsetFor(const char* key) const;
  int _maxLenFor(const char* key) const;
  bool _isInt(const char* key) const;
  template<typename T> T _getScalar(const char* key, T defaultValue);
  template<typename T> bool _putScalar(const char* key, T value);
};

#pragma once

#include <Arduino.h>
#include "config.h"

class RelayCtrl {
public:
  void setPin(int8_t pin);   // call before begin()
  int8_t pin() const { return _pin; }
  void begin();
  void set(bool on);
  bool isOn();

private:
  int8_t _pin = DEFAULT_RELAY_PIN;
};

extern RelayCtrl relays;

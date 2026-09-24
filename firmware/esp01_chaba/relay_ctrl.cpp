#include "relay_ctrl.h"
#include "state.h"
#include "config.h"

RelayCtrl relays;

void RelayCtrl::setPin(int8_t pin) {
  _pin = pin;
}

void RelayCtrl::begin() {
  pinMode(_pin, OUTPUT);
#if RELAY_ACTIVE_LOW
  digitalWrite(_pin, HIGH);
#else
  digitalWrite(_pin, LOW);
#endif
  g_state.relayOn = false;
}

void RelayCtrl::set(bool on) {
#if RELAY_ACTIVE_LOW
  digitalWrite(_pin, on ? LOW : HIGH);
#else
  digitalWrite(_pin, on ? HIGH : LOW);
#endif
  g_state.relayOn = on;
}

bool RelayCtrl::isOn() {
  return g_state.relayOn;
}

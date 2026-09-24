// ESP-01 minimal bring-up sketch v2 — bisection tool, not part of the product.
//
// v2 change: the LED blinks BEFORE any Serial output. If Serial hangs
// (the old user_uart_wait_tx_fifo_empty class of failure), the LED
// freezes in a visible state instead of the test dying silently.
//
// WIRING REQUIREMENT: any LED on GPIO2 MUST have a series resistor
// (330 ohm - 1 k ohm). Without it, driving the pin LOW overcurrents
// the GPIO and can reset the chip. 3V3 -> resistor -> LED -> GPIO2.
//
// Expected on a 115200 monitor after reset:
//   1. one burst of garbage (~0.3 s) — ROM bootloader at 74880, normal
//   2. then repeating, readable:
//        minimal: alive ms=600
//        minimal: alive ms=1200
//        ...
//
// LED (external, 3V3 -> resistor -> LED -> GPIO2) shows, with no
// monitor attached at all:
//   - continuous blinking forever ......... app running
//   - blinks twice then freezes OFF ....... Serial print hung (UART issue)
//   - brief flicker, pause, repeat ........ still boot-looping (power/RST)
//
// Board settings (same as esp01_chaba): Generic ESP8266 Module,
// Flash mode DOUT, Flash size 1MB, CPU 80 MHz, Upload 115200.

void setup() {
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);  // LED off at boot (active-low wiring)
  Serial.begin(115200);
}

void loop() {
  digitalWrite(2, LOW);   // LED on
  delay(300);
  digitalWrite(2, HIGH);  // LED off
  delay(300);
  Serial.print("minimal: alive ms=");
  Serial.println(millis());
}

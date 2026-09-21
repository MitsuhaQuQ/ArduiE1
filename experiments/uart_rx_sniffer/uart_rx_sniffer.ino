#include <Arduino.h>

// Passive one-direction hardware-UART recorder for Arduino UNO R4 WiFi.
// Connect the protected SHUTTER / DATA-B node to D0 (Serial1 RX).
// Leave D1 (Serial1 TX) completely unconnected from the camera circuit.

constexpr uint32_t CAPTURE_BAUD = 9600;
bool capturing = false;

static void beginCapture() {
  static const uint8_t magic[8] = {'E', 'O', 'S', '1', 'R', 'X', 'B', '1'};
  while (Serial1.available()) Serial1.read();
  Serial.write(magic, sizeof(magic));
  Serial.write(reinterpret_cast<const uint8_t *>(&CAPTURE_BAUD), sizeof(CAPTURE_BAUD));
  Serial.flush();
  capturing = true;
}

void setup() {
  Serial.begin(1000000);
  Serial1.begin(CAPTURE_BAUD, SERIAL_8N1);
}

void loop() {
  while (Serial.available()) {
    const int command = Serial.read();
    if (command == 'S' || command == 's') beginCapture();
    if (command == 'X' || command == 'x') capturing = false;
  }

  uint8_t output[80];
  uint8_t used = 0;
  while (capturing && Serial1.available() && used <= sizeof(output) - 5) {
    const uint32_t timestamp = micros();
    const uint8_t value = static_cast<uint8_t>(Serial1.read());
    memcpy(output + used, &timestamp, sizeof(timestamp));
    output[used + 4] = value;
    used += 5;
  }
  if (used) Serial.write(output, used);
}

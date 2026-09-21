#include <Arduino.h>

uint32_t lastMarker = 0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  while (Serial.available()) Serial.write(Serial.read());
  if (millis() - lastMarker >= 1000) {
    lastMarker = millis();
    Serial.write("RA4\n");
  }
}

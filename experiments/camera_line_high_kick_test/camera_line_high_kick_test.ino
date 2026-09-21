#include <Arduino.h>

// EOS DATA-A recovery experiment.
// D4 -- 1k -- FOCUS/DATA-A -- 5.1k -- D2
// GND -- COMMON. D0/D1 disconnected.
// Idle is high impedance. After a long LOW, briefly drive HIGH, then release.

constexpr uint8_t TX_PIN = 4;
constexpr uint8_t SENSE_PIN = 2;

static void releaseLine() {
  pinMode(TX_PIN, INPUT);
  digitalWrite(TX_PIN, LOW);
}

static void runOne(uint32_t lowUs, uint32_t highKickUs) {
  digitalWrite(TX_PIN, LOW);
  pinMode(TX_PIN, OUTPUT);
  delayMicroseconds(lowUs);

  digitalWrite(TX_PIN, HIGH);
  delayMicroseconds(highKickUs);
  releaseLine();
  delayMicroseconds(100);

  Serial.print("LOW "); Serial.print(lowUs);
  Serial.print(" us, HIGH kick "); Serial.print(highKickUs);
  Serial.print(" us, after release: ");
  Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW");
  delay(100);
}

void setup() {
  releaseLine();
  pinMode(SENSE_PIN, INPUT);
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("EOS DATA-A HIGH-kick test ready");
  Serial.println("Confirm PC mode, then send t once");
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 't' || c == 'T') {
      Serial.print("Initial idle: ");
      Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW - stop");
      if (!digitalRead(SENSE_PIN)) continue;
      runOne(208, 10);
      runOne(312, 10);
      runOne(312, 20);
      Serial.print("Final idle: ");
      Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW");
    }
  }
}

#include <Arduino.h>

// Measures EOS FOCUS/DATA-A release time using the camera's own pull-up.
// No external 5V pull-up is used.
// C -- 330 ohm -- FOCUS/DATA-A -- 5.1k -- D2
// E -- COMMON/GND, D4 -- 10k -- B, B -- 100k -- E

constexpr uint8_t DRIVE_PIN = 4;
constexpr uint8_t SENSE_PIN = 2;
constexpr uint32_t CPU_HZ = 48000000UL;

static void beginCycleCounter() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void measure(uint32_t lowMicros) {
  digitalWrite(DRIVE_PIN, HIGH);
  delayMicroseconds(lowMicros);
  const uint32_t start = DWT->CYCCNT;
  digitalWrite(DRIVE_PIN, LOW);
  const uint32_t timeout = CPU_HZ / 100; // 10 ms
  while (!digitalRead(SENSE_PIN)) {
    if (DWT->CYCCNT - start >= timeout) {
      Serial.print(lowMicros);
      Serial.println(" us low: rise >10000 us (FAILED)");
      return;
    }
  }
  const uint32_t cycles = DWT->CYCCNT - start;
  Serial.print(lowMicros);
  Serial.print(" us low: rise ");
  Serial.print(cycles);
  Serial.print(" cycles, about ");
  Serial.print(static_cast<float>(cycles) / 48.0f, 2);
  Serial.println(" us");
}

void setup() {
  digitalWrite(DRIVE_PIN, LOW);
  pinMode(DRIVE_PIN, OUTPUT);
  pinMode(SENSE_PIN, INPUT);
  beginCycleCounter();
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("EOS DATA-A rise test ready; no external 5V pull-up");
  Serial.println("Confirm camera PC mode, then send t once");
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 't' || c == 'T') {
      Serial.print("Idle: ");
      Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW - stop");
      if (!digitalRead(SENSE_PIN)) continue;
      measure(104);
      delay(100);
      measure(312);
      delay(100);
      Serial.print("Final idle: ");
      Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW");
    }
  }
}

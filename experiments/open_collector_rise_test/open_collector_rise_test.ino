#include <Arduino.h>

// Offline rise-time test. DO NOT connect the EOS camera during this test.
//
// D4 -- 10k -- NPN base
// base -- 100k -- emitter
// emitter -- UNO GND
// collector -- 1k -- TEST_NODE
// UNO 5V -- 10k -- TEST_NODE  (temporary replacement for camera pull-up)
// TEST_NODE -- 5.1k -- D2

constexpr uint8_t DRIVE_PIN = 4;
constexpr uint8_t SENSE_PIN = 2;
constexpr uint32_t CPU_HZ = 48000000UL;

static void beginCycleCounter() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t measureRelease(uint32_t lowMicros) {
  digitalWrite(DRIVE_PIN, HIGH); // transistor on, TEST_NODE low
  delayMicroseconds(lowMicros);

  const uint32_t start = DWT->CYCCNT;
  digitalWrite(DRIVE_PIN, LOW);  // transistor off, release TEST_NODE
  const uint32_t timeoutCycles = CPU_HZ / 10; // 100 ms
  while (!digitalRead(SENSE_PIN)) {
    if (DWT->CYCCNT - start >= timeoutCycles) return UINT32_MAX;
  }
  return DWT->CYCCNT - start;
}

static void runOne(uint32_t lowMicros) {
  const uint32_t cycles = measureRelease(lowMicros);
  Serial.print("Low pulse ");
  Serial.print(lowMicros);
  Serial.print(" us: release-to-HIGH ");
  if (cycles == UINT32_MAX) {
    Serial.println(">100000 us (FAILED)");
  } else {
    Serial.print(cycles);
    Serial.print(" cycles, about ");
    Serial.print(static_cast<float>(cycles) / 48.0f, 2);
    Serial.println(" us");
  }
  digitalWrite(DRIVE_PIN, LOW);
  delay(100);
}

void setup() {
  digitalWrite(DRIVE_PIN, LOW);
  pinMode(DRIVE_PIN, OUTPUT);
  pinMode(SENSE_PIN, INPUT);
  beginCycleCounter();
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("Offline open-collector rise test ready");
  Serial.println("Camera must be disconnected. Send t to run.");
}

void loop() {
  while (Serial.available()) {
    const char command = Serial.read();
    if (command == 't' || command == 'T') {
      Serial.print("Idle before test: ");
      Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW (check wiring)");
      runOne(104);
      runOne(312);
      runOne(1000);
      Serial.print("Idle after test: ");
      Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW (FAILED)");
    }
  }
}

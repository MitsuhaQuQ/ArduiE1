#include <Arduino.h>

// Minimal active EOS-1V N3 protocol probe for UNO R4 WiFi.
//
// Wiring:
//   D4 -- 10k -- NPN base
//   base -- 100k -- emitter
//   emitter -- EOS COMMON and Arduino GND
//   collector -- 1k -- EOS FOCUS / DATA-A
//   EOS SHUTTER / DATA-B -- 5.1k -- D0 / Serial1 RX
//   D1 / Serial1 TX is not connected.
//
// Open collector inversion:
//   D4 HIGH = DATA-A low
//   D4 LOW  = DATA-A released/high

constexpr uint8_t DRIVE_PIN = 4;
constexpr uint32_t CAMERA_BAUD = 9600;
constexpr uint32_t CPU_HZ = 48000000UL;
constexpr uint32_t CYCLES_PER_BIT = CPU_HZ / CAMERA_BAUD; // exactly 5000
constexpr uint32_t REPLY_TIMEOUT_MS = 300;

static inline void dataLow() {
  digitalWrite(DRIVE_PIN, HIGH);
}

static inline void dataRelease() {
  digitalWrite(DRIVE_PIN, LOW);
}

static void beginCycleCounter() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void sendByteOpenCollector(uint8_t value) {
  // 8-N-1, LSB first. The Cortex-M4 cycle counter keeps running while
  // interrupts are disabled, unlike the Arduino timekeeping functions.
  uint32_t boundary = DWT->CYCCNT;
  noInterrupts();
  dataLow();                         // start bit
  for (uint8_t slot = 0; slot < 9; ++slot) {
    boundary += CYCLES_PER_BIT;
    while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}
    if (slot < 8) {
      if (value & (1u << slot)) dataRelease();
      else dataLow();
    } else {
      dataRelease();                 // stop bit and idle
    }
  }
  boundary += CYCLES_PER_BIT;
  while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}
  interrupts();
}

static void drainCameraRx() {
  while (Serial1.available()) Serial1.read();
}

static void runProbe() {
  drainCameraRx();
  Serial.println("TX FF");
  sendByteOpenCollector(0xFF);

  const uint32_t deadline = millis() + REPLY_TIMEOUT_MS;
  bool received = false;
  bool first = true;
  Serial.print("RX");
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    while (Serial1.available()) {
      const uint8_t value = static_cast<uint8_t>(Serial1.read());
      Serial.print(first ? " " : " ");
      if (value < 0x10) Serial.print('0');
      Serial.print(value, HEX);
      first = false;
      received = true;
    }
  }
  Serial.println();

  if (!received) Serial.println("RESULT: timeout; line remains released");
  else Serial.println("Expected first reply for this probe: F4");
}

void setup() {
  // Establish the inactive level before enabling the output driver.
  digitalWrite(DRIVE_PIN, LOW);
  pinMode(DRIVE_PIN, OUTPUT);
  beginCycleCounter();

  Serial1.begin(CAMERA_BAUD, SERIAL_8N1);
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("EOS-1V FF probe ready; DATA-A is released");
  Serial.println("With camera in PC mode, send c exactly once");
}

void loop() {
  while (Serial.available()) {
    const char command = Serial.read();
    if (command == 'c' || command == 'C') runProbe();
  }
}

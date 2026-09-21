#include <Arduino.h>

// Offline UART loopback. Camera must remain disconnected.
// D4 -- 10k -- base, base -- 100k -- emitter, emitter -- GND
// collector -- 1k -- TEST_NODE
// 5V -- 10k -- TEST_NODE
// TEST_NODE -- 5.1k -- D0 / Serial1 RX
// D1 / Serial1 TX remains unconnected.

constexpr uint8_t DRIVE_PIN = 4;
constexpr uint32_t CPU_HZ = 48000000UL;
constexpr uint32_t CYCLES_PER_BIT = CPU_HZ / 9600UL;

static void beginCycleCounter() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void sendByte(uint8_t value) {
  uint32_t boundary = DWT->CYCCNT;
  digitalWrite(DRIVE_PIN, HIGH); // start bit: transistor on, line low
  for (uint8_t bit = 0; bit < 9; ++bit) {
    boundary += CYCLES_PER_BIT;
    while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}
    if (bit < 8) digitalWrite(DRIVE_PIN, (value & (1u << bit)) ? LOW : HIGH);
    else digitalWrite(DRIVE_PIN, LOW); // stop bit: release/high
  }
  boundary += CYCLES_PER_BIT;
  while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}
}

static void electricalCheck() {
  digitalWrite(DRIVE_PIN, LOW);
  delay(2);
  Serial.print("D0 idle: ");
  Serial.println(digitalRead(0) ? "HIGH" : "LOW (wiring error)");

  digitalWrite(DRIVE_PIN, HIGH);
  delay(5);
  Serial.print("D0 while transistor ON: ");
  Serial.println(digitalRead(0) ? "HIGH (wiring error)" : "LOW");

  digitalWrite(DRIVE_PIN, LOW);
  delay(2);
  Serial.print("D0 after release: ");
  Serial.println(digitalRead(0) ? "HIGH" : "LOW (wiring error)");
}

static void printHex(uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

static void testByte(uint8_t value) {
  while (Serial1.available()) Serial1.read();
  Serial.print("TX ");
  printHex(value);
  sendByte(value);

  const uint32_t deadline = millis() + 50;
  while (!Serial1.available() && static_cast<int32_t>(millis() - deadline) < 0) {}
  if (Serial1.available()) {
    const uint8_t received = static_cast<uint8_t>(Serial1.read());
    Serial.print(" -> RX ");
    printHex(received);
    Serial.println(received == value ? " OK" : " MISMATCH");
  } else {
    Serial.println(" -> RX TIMEOUT");
  }
  delay(50);
}

void setup() {
  digitalWrite(DRIVE_PIN, LOW);
  pinMode(DRIVE_PIN, OUTPUT);
  beginCycleCounter();
  Serial1.begin(9600, SERIAL_8N1);
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("Offline UART loopback ready; camera must be disconnected");
  Serial.println("Send t to test FF F4 F6 F1");
}

void loop() {
  while (Serial.available()) {
    const char command = Serial.read();
    if (command == 't' || command == 'T') {
      electricalCheck();
      testByte(0xFF);
      testByte(0xF4);
      testByte(0xF6);
      testByte(0xF1);
    }
  }
}

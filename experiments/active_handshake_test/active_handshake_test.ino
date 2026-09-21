#include <Arduino.h>

// EOS-1V N3 verified handshake and clean-exit test for UNO R4 WiFi.
// Uses the same open-collector circuit as active_ff_probe.

constexpr uint8_t DRIVE_PIN = 4;
constexpr uint8_t SENSE_PIN = 2;
constexpr uint32_t CAMERA_BAUD = 9600;
constexpr uint32_t CPU_HZ = 48000000UL;
constexpr uint32_t CYCLES_PER_BIT = CPU_HZ / CAMERA_BAUD; // exactly 5000
uint32_t lastTxCycles = 0;
uint32_t lastTxMicros = 0;

static inline void releaseDataA() { digitalWrite(DRIVE_PIN, LOW); }
static inline void pullDataALow() { digitalWrite(DRIVE_PIN, HIGH); }

static void beginCycleCounter() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void sendCameraByte(uint8_t value) {
  // Use the Cortex-M4 hardware cycle counter. It continues while interrupts
  // are disabled, giving a stable 5000 CPU cycles per 9600-baud bit.
  const uint32_t startMicros = micros();
  const uint32_t startCycles = DWT->CYCCNT;
  uint32_t boundary = startCycles;
  noInterrupts();
  pullDataALow();
  for (uint8_t slot = 0; slot < 9; ++slot) {
    boundary += CYCLES_PER_BIT;
    while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}
    if (slot < 8) {
      if (value & (1u << slot)) releaseDataA();
      else pullDataALow();
    } else {
      releaseDataA();
    }
  }
  boundary += CYCLES_PER_BIT;
  while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}
  interrupts();
  lastTxCycles = DWT->CYCCNT - startCycles;
  lastTxMicros = micros() - startMicros;
}

static void printDataAIdle() {
  Serial.print("DATA-A idle after TX: ");
  Serial.print(digitalRead(SENSE_PIN) ? "HIGH" : "LOW");
  Serial.println();
}

static void printTxTiming() {
  Serial.print("Last TX duration: ");
  Serial.print(lastTxCycles);
  Serial.print(" cycles, ");
  Serial.print(lastTxMicros);
  Serial.println(" us");
}

static void printByte(uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

static void drainRx() {
  while (Serial1.available()) Serial1.read();
}

static size_t receiveQuiet(uint8_t *buffer, size_t count, uint32_t timeoutMs) {
  size_t used = 0;
  const uint32_t deadline = millis() + timeoutMs;
  while (used < count && static_cast<int32_t>(millis() - deadline) < 0) {
    if (Serial1.available()) buffer[used++] = static_cast<uint8_t>(Serial1.read());
  }
  return used;
}

static void printPacket(const char *label, const uint8_t *buffer,
                        size_t used, size_t expected) {
  Serial.print(label);
  Serial.print(" (");
  Serial.print(used);
  Serial.print('/');
  Serial.print(expected);
  Serial.print("):");
  for (size_t i = 0; i < used; ++i) {
    Serial.print(' ');
    printByte(buffer[i]);
  }
  Serial.println();
}

static bool receiveBytes(uint8_t *buffer, size_t count, uint32_t timeoutMs) {
  size_t used = 0;
  const uint32_t deadline = millis() + timeoutMs;
  while (used < count && static_cast<int32_t>(millis() - deadline) < 0) {
    if (Serial1.available()) buffer[used++] = static_cast<uint8_t>(Serial1.read());
  }
  Serial.print("RX (");
  Serial.print(used);
  Serial.print('/');
  Serial.print(count);
  Serial.print("):");
  for (size_t i = 0; i < used; ++i) {
    Serial.print(' ');
    printByte(buffer[i]);
  }
  Serial.println();
  return used == count;
}

static bool sendAndExpect(uint8_t command, uint8_t expectedFirst,
                          size_t expectedLength, uint32_t timeoutMs) {
  uint8_t reply[32];
  if (expectedLength > sizeof(reply)) return false;
  Serial.print("TX: ");
  printByte(command);
  Serial.println();
  sendCameraByte(command);
  const bool complete = receiveBytes(reply, expectedLength, timeoutMs);
  printDataAIdle();
  if (!complete) return false;
  if (reply[0] != expectedFirst) {
    Serial.print("Unexpected first byte; wanted ");
    printByte(expectedFirst);
    Serial.println();
    return false;
  }
  return true;
}

static bool cleanExit() {
  uint8_t firstReply[1];
  uint8_t finalReply[1];
  drainRx();
  sendCameraByte(0xF2);
  const size_t firstUsed = receiveQuiet(firstReply, 1, 500);
  if (firstUsed != 1 || firstReply[0] != 0xF4) {
    printPacket("EXIT RX1", firstReply, firstUsed, 1);
    return false;
  }
  // The acknowledgement is timing-sensitive; send before any USB logging.
  sendCameraByte(0xF4);
  delay(305);
  sendCameraByte(0xF2);
  const size_t finalUsed = receiveQuiet(finalReply, 1, 500);
  printPacket("EXIT RX1", firstReply, firstUsed, 1);
  printPacket("EXIT RX2", finalReply, finalUsed, 1);
  return finalUsed == 1 && finalReply[0] == 0xF2;
}

static void runHandshakeTest() {
  uint8_t ffReply[1];
  uint8_t f6Reply[17];
  uint8_t f1Reply[6];
  drainRx();
  Serial.println("Running timing-critical handshake; output resumes when complete");

  sendCameraByte(0xFF);
  const size_t ffUsed = receiveQuiet(ffReply, sizeof(ffReply), 500);
  if (ffUsed != 1 || ffReply[0] != 0xF4) {
    printPacket("FF RX", ffReply, ffUsed, sizeof(ffReply));
    Serial.println("STOP: FF/F4 probe failed; DATA-A released");
    return;
  }

  // Original ES-E1 acknowledges F4 immediately, without USB/UI work between.
  sendCameraByte(0xF4);
  delay(302);

  sendCameraByte(0xF6);
  const size_t f6Used = receiveQuiet(f6Reply, sizeof(f6Reply), 500);
  if (f6Used != sizeof(f6Reply) || f6Reply[0] != 0xF6) {
    printPacket("FF RX", ffReply, ffUsed, sizeof(ffReply));
    printPacket("F6 RX", f6Reply, f6Used, sizeof(f6Reply));
    printTxTiming();
    printDataAIdle();
    Serial.println("STOP: F6 identification reply failed; DATA-A released");
    return;
  }

  // The original sends F1 immediately after receiving the complete F6 packet.
  sendCameraByte(0xF1);
  const size_t f1Used = receiveQuiet(f1Reply, sizeof(f1Reply), 500);
  printPacket("FF RX", ffReply, ffUsed, sizeof(ffReply));
  printPacket("F6 RX", f6Reply, f6Used, sizeof(f6Reply));
  printPacket("F1 RX", f1Reply, f1Used, sizeof(f1Reply));
  printDataAIdle();
  if (f1Used != sizeof(f1Reply) || f1Reply[0] != 0xF1) {
    Serial.println("STOP: F1 identification reply failed; attempting clean exit");
    cleanExit();
    return;
  }

  Serial.println("HANDSHAKE OK");
  delay(100);
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; DATA-A is released");
}

static void runF1Test() {
  uint8_t ffReply[1];
  uint8_t f1Reply[6];
  drainRx();
  Serial.println("Running FF/F4/F1 transmit test; output resumes when complete");

  sendCameraByte(0xFF);
  const size_t ffUsed = receiveQuiet(ffReply, sizeof(ffReply), 500);
  if (ffUsed != 1 || ffReply[0] != 0xF4) {
    printPacket("FF RX", ffReply, ffUsed, sizeof(ffReply));
    Serial.println("STOP: FF/F4 probe failed");
    return;
  }

  sendCameraByte(0xF4); // immediate acknowledgement
  delay(63);            // observed alternate-session F4-to-F1 interval
  sendCameraByte(0xF1);
  const size_t f1Used = receiveQuiet(f1Reply, sizeof(f1Reply), 500);

  printPacket("FF RX", ffReply, ffUsed, sizeof(ffReply));
  printPacket("F1 RX", f1Reply, f1Used, sizeof(f1Reply));
  printTxTiming();
  printDataAIdle();
  if (f1Used == sizeof(f1Reply) && f1Reply[0] == 0xF1) {
    Serial.println("F1 TEST OK; attempting clean exit");
    if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
    else Serial.println("EXIT FAILED; DATA-A is released");
  } else {
    Serial.println("STOP: F1 command was not accepted; DATA-A released");
  }
}

void setup() {
  digitalWrite(DRIVE_PIN, LOW);
  pinMode(DRIVE_PIN, OUTPUT);
  pinMode(SENSE_PIN, INPUT);
  beginCycleCounter();
  Serial1.begin(9600, SERIAL_8N1);
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("EOS-1V handshake test ready; DATA-A is released");
  Serial.println("Confirm PC mode, then send h exactly once");
  Serial.println("For the shorter F1 transmit diagnostic, send q exactly once");
}

void loop() {
  while (Serial.available()) {
    const char command = Serial.read();
    if (command == 'h' || command == 'H') runHandshakeTest();
    if (command == 'q' || command == 'Q') runF1Test();
  }
}

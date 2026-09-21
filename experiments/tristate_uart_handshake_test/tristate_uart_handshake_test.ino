#include <Arduino.h>

// EOS-1V adaptive open-drain UART test.
// D4 -- 1k -- EOS FOCUS / DATA-A
// D0 / RX -- 5.1k -- EOS SHUTTER / DATA-B
// GND -- EOS COMMON
// D1, D2 and transistor circuit are disconnected.
// A single LOW slot is followed by a normal high-impedance release. After
// two or more consecutive LOW slots, HIGH is driven for 10 us before release
// because the camera disables its weak pull-up during a longer LOW interval.

constexpr uint8_t TX_PIN = 4;
constexpr uint32_t CPU_HZ = 48000000UL;
constexpr uint32_t CYCLES_PER_BIT = CPU_HZ / 9600UL;
constexpr uint32_t HIGH_KICK_CYCLES = CPU_HZ / 100000UL; // 10 us

static void beginCycleCounter() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void releaseTx() {
  pinMode(TX_PIN, INPUT);
  digitalWrite(TX_PIN, LOW); // ensure internal pull-up is disabled
}

static void enterLow() {
  digitalWrite(TX_PIN, LOW);
  pinMode(TX_PIN, OUTPUT);
}

static void enterHigh(uint8_t precedingLowSlots) {
  if (precedingLowSlots >= 2) {
    digitalWrite(TX_PIN, HIGH);
    const uint32_t kickEnd = DWT->CYCCNT + HIGH_KICK_CYCLES;
    while (static_cast<int32_t>(DWT->CYCCNT - kickEnd) < 0) {}
  }
  releaseTx();
}

static void sendByte(uint8_t value) {
  // Start bit is one LOW slot. High bits normally use high impedance; only a
  // transition after a long LOW run receives a short active-HIGH recovery.
  enterLow();
  uint32_t boundary = DWT->CYCCNT;
  uint8_t lowSlots = 1;

  noInterrupts();
  for (uint8_t bit = 0; bit < 8; ++bit) {
    boundary += CYCLES_PER_BIT;
    while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}
    if (value & (1u << bit)) {
      if (lowSlots) enterHigh(lowSlots);
      lowSlots = 0;
    } else {
      if (!lowSlots) enterLow();
      ++lowSlots;
    }
  }

  // Stop bit.
  boundary += CYCLES_PER_BIT;
  while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}
  if (lowSlots) enterHigh(lowSlots);

  // Hold the released stop bit for one complete slot.
  boundary += CYCLES_PER_BIT;
  while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}
  interrupts();
}

static size_t receiveQuiet(uint8_t *data, size_t count, uint32_t timeoutMs) {
  size_t used = 0;
  const uint32_t deadline = millis() + timeoutMs;
  while (used < count && static_cast<int32_t>(millis() - deadline) < 0) {
    if (Serial1.available()) data[used++] = static_cast<uint8_t>(Serial1.read());
  }
  return used;
}

static void drainRx() { while (Serial1.available()) Serial1.read(); }

static void printHex(uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

static void printPacket(const char *label, const uint8_t *data,
                        size_t used, size_t expected) {
  Serial.print(label);
  Serial.print(" ("); Serial.print(used); Serial.print('/');
  Serial.print(expected); Serial.print("):");
  for (size_t i = 0; i < used; ++i) {
    Serial.print(' '); printHex(data[i]);
  }
  Serial.println();
}

static bool cleanExit() {
  uint8_t a[1], b[1];
  drainRx();
  sendByte(0xF2);
  const size_t na = receiveQuiet(a, 1, 500);
  if (na != 1 || a[0] != 0xF4) { printPacket("EXIT RX1", a, na, 1); return false; }
  sendByte(0xF4);
  delay(305);
  sendByte(0xF2);
  const size_t nb = receiveQuiet(b, 1, 500);
  printPacket("EXIT RX1", a, na, 1);
  printPacket("EXIT RX2", b, nb, 1);
  return nb == 1 && b[0] == 0xF2;
}

static void runTest() {
  uint8_t ff[1], f1[6];
  drainRx();
  Serial.println("Running adaptive UART FF/F4/F1 test");
  sendByte(0xFF);
  const size_t nff = receiveQuiet(ff, 1, 500);
  if (nff != 1 || ff[0] != 0xF4) {
    printPacket("FF RX", ff, nff, 1);
    Serial.println("STOP: initial probe failed; TX returned to high impedance");
    return;
  }
  sendByte(0xF4);
  delay(63);
  sendByte(0xF1);
  const size_t nf1 = receiveQuiet(f1, 6, 500);
  printPacket("FF RX", ff, nff, 1);
  printPacket("F1 RX", f1, nf1, 6);
  if (nf1 == 6 && f1[0] == 0xF1) {
    Serial.println("ADAPTIVE UART TEST OK");
    if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
    else Serial.println("EXIT FAILED; TX is high impedance");
  } else {
    Serial.println("STOP: F1 reply failed; TX returned to high impedance");
  }
}

void setup() {
  releaseTx();
  beginCycleCounter();
  Serial1.begin(9600, SERIAL_8N1); // D0 RX used; D1 remains physically unconnected
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("EOS-1V adaptive UART test ready");
  Serial.println("Confirm PC mode, then send q exactly once");
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 'q' || c == 'Q') runTest();
  }
}

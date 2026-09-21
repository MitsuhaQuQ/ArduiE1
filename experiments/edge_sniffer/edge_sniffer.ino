#include <Arduino.h>

// Passive EOS N3 data-line edge recorder for Arduino UNO R4 WiFi.
// D2: FOCUS / DATA-A through a protected high-impedance input.
// D3: SHUTTER / DATA-B through a protected high-impedance input.
// Never enable pull-ups and never connect an Arduino output to either line.

constexpr uint8_t PIN_DATA_A = 2;
constexpr uint8_t PIN_DATA_B = 3;
constexpr uint16_t RING_SIZE = 2048;
constexpr uint16_t RING_MASK = RING_SIZE - 1;
constexpr uint32_t TIME_MASK = 0x3FFFFFFFUL;
constexpr uint32_t CHANNEL_BIT = 0x80000000UL;
constexpr uint32_t LEVEL_BIT = 0x40000000UL;

volatile uint32_t ringBuffer[RING_SIZE];
volatile uint16_t ringHead = 0;
volatile uint16_t ringTail = 0;
volatile uint32_t droppedEvents = 0;
volatile bool capturing = false;

static inline void recordEdge(uint8_t channel, uint8_t pin) {
  if (!capturing) return;

  const uint32_t timestamp = micros() & TIME_MASK;
  const uint32_t value = timestamp |
                         (channel ? CHANNEL_BIT : 0) |
                         (digitalRead(pin) ? LEVEL_BIT : 0);
  const uint16_t next = (ringHead + 1) & RING_MASK;
  if (next == ringTail) {
    ++droppedEvents;
    return;
  }
  ringBuffer[ringHead] = value;
  ringHead = next;
}

void dataAChanged() { recordEdge(0, PIN_DATA_A); }
void dataBChanged() { recordEdge(1, PIN_DATA_B); }

static void beginCapture() {
  static const uint8_t magic[8] = {'E', 'O', 'S', '1', 'E', 'D', 'G', 'E'};

  capturing = false;
  noInterrupts();
  ringHead = 0;
  ringTail = 0;
  droppedEvents = 0;
  interrupts();

  Serial.write(magic, sizeof(magic));
  Serial.flush();

  noInterrupts();
  const uint32_t now = micros() & TIME_MASK;
  ringBuffer[0] = now | (digitalRead(PIN_DATA_A) ? LEVEL_BIT : 0);
  ringBuffer[1] = now | CHANNEL_BIT | (digitalRead(PIN_DATA_B) ? LEVEL_BIT : 0);
  ringHead = 2;
  ringTail = 0;
  capturing = true;
  interrupts();
}

static void stopCapture() {
  capturing = false;
}

static void drainEvents() {
  uint32_t batch[32];
  uint8_t count = 0;

  noInterrupts();
  while (ringTail != ringHead && count < 32) {
    batch[count++] = ringBuffer[ringTail];
    ringTail = (ringTail + 1) & RING_MASK;
  }
  interrupts();

  if (count) Serial.write(reinterpret_cast<uint8_t *>(batch), count * sizeof(uint32_t));

  uint32_t lost = 0;
  noInterrupts();
  if (droppedEvents) {
    lost = droppedEvents;
    droppedEvents = 0;
  }
  interrupts();

  if (lost) {
    const uint32_t marker[2] = {0xFFFFFFFFUL, lost};
    Serial.write(reinterpret_cast<const uint8_t *>(marker), sizeof(marker));
  }
}

void setup() {
  pinMode(PIN_DATA_A, INPUT);
  pinMode(PIN_DATA_B, INPUT);
  Serial.begin(1000000);
  attachInterrupt(digitalPinToInterrupt(PIN_DATA_A), dataAChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_DATA_B), dataBChanged, CHANGE);
}

void loop() {
  while (Serial.available()) {
    const int command = Serial.read();
    if (command == 'S' || command == 's') beginCapture();
    if (command == 'X' || command == 'x') stopCapture();
  }
  drainEvents();
}

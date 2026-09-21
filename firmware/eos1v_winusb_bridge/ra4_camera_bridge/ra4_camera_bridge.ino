#include <Arduino.h>

// Camera-side circuit is identical to the validated eos1v_interface project:
// D4 -> 10k -> NPN base, base -> 100k -> emitter, emitter -> COMMON,
// collector -> 330R -> DATA-A.
// D5 -> 1k -> 1N4007 anode, diode cathode/stripe -> DATA-A.
// DATA-B -> 5.1k -> D0/RX. D1/D2 remain disconnected.

namespace {

constexpr uint8_t kLowDrivePin = 4;
constexpr uint8_t kHighAssistPin = 5;
constexpr uint8_t kSensePin = 2;
constexpr uint32_t kCpuHz = 48000000UL;
constexpr uint32_t kCyclesPerBit = kCpuHz / 9600UL;
constexpr uint32_t kHostBaud = 115200;
constexpr uint16_t kMaxPayload = 512;
constexpr uint16_t kMaxCameraTx = 64;
constexpr uint8_t kVersion = 1;
constexpr uint32_t kDriverIdleReleaseMs = 30000;

constexpr uint8_t kTypePing = 0x01;
constexpr uint8_t kTypeGetStatus = 0x02;
constexpr uint8_t kTypeExchange = 0x10;
constexpr uint8_t kTypeRelease = 0x11;
constexpr uint8_t kResponseBit = 0x80;

constexpr uint8_t kStatusOk = 0x00;
constexpr uint8_t kStatusUnknownType = 0x01;
constexpr uint8_t kStatusInvalidPayload = 0x02;
constexpr uint8_t kStatusCrcError = 0x03;
constexpr uint8_t kStatusCameraTimeout = 0x04;
constexpr uint8_t kStatusLimitExceeded = 0x05;

bool highAssistActive = false;
bool cycleCounterStarted = false;
bool cameraSerialStarted = false;
bool cameraHardwareStarted = false;
uint32_t exchangeCount = 0;
uint32_t framingErrorCount = 0;
uint32_t lastValidRequestMs = 0;

uint16_t readLe16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(data[1] << 8);
}

void writeLe16(uint8_t *data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
}

void writeLe32(uint8_t *data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void beginCycleCounter() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  cycleCounterStarted = true;
}

inline void driveLow() {
  digitalWrite(kHighAssistPin, LOW);
  digitalWrite(kLowDrivePin, HIGH);
}

inline void releaseLine() {
  digitalWrite(kLowDrivePin, LOW);
  digitalWrite(kHighAssistPin, LOW);
}

void enterHigh(uint8_t precedingLowSlots) {
  digitalWrite(kLowDrivePin, LOW);
  if (highAssistActive || precedingLowSlots >= 2) {
    highAssistActive = true;
    digitalWrite(kHighAssistPin, HIGH);
  } else {
    digitalWrite(kHighAssistPin, LOW);
  }
}

void sendCameraByte(uint8_t value) {
  driveLow();
  const uint32_t frameStart = DWT->CYCCNT;
  uint8_t lowSlots = 1;
  noInterrupts();

  for (uint8_t slot = 0; slot < 10; ++slot) {
    const uint32_t boundary = frameStart + (slot + 1) * kCyclesPerBit;
    while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}

    if (slot < 8) {
      if (value & (1u << slot)) {
        if (lowSlots != 0) enterHigh(lowSlots);
        lowSlots = 0;
      } else {
        if (lowSlots == 0) driveLow();
        ++lowSlots;
      }
    } else if (slot == 8) {
      if (lowSlots != 0) enterHigh(lowSlots);
      lowSlots = 0;
    } else if (highAssistActive) {
      digitalWrite(kLowDrivePin, LOW);
      digitalWrite(kHighAssistPin, HIGH);
    } else {
      releaseLine();
    }
  }
  interrupts();
}

void drainCameraRx() {
  if (!cameraSerialStarted) return;
  while (Serial1.available()) Serial1.read();
}

void ensureCameraSerial() {
  if (cameraSerialStarted) return;
  Serial1.begin(9600, SERIAL_8N1);
  cameraSerialStarted = true;
}

void ensureCameraHardware() {
  if (cameraHardwareStarted) return;
  pinMode(kLowDrivePin, OUTPUT);
  pinMode(kHighAssistPin, OUTPUT);
  pinMode(kSensePin, INPUT_PULLUP);
  releaseLine();
  ensureCameraSerial();
  cameraHardwareStarted = true;
}

size_t receiveCamera(uint8_t *destination, size_t expected,
                     uint16_t firstTimeoutMs, uint16_t interByteTimeoutMs) {
  size_t used = 0;
  uint32_t deadline = millis() + firstTimeoutMs;
  while (used < expected) {
    if (Serial1.available()) {
      destination[used++] = static_cast<uint8_t>(Serial1.read());
      deadline = millis() + interByteTimeoutMs;
    } else if (static_cast<int32_t>(millis() - deadline) >= 0) {
      break;
    }
  }
  return used;
}

void writeFrame(uint8_t type, uint16_t sequence,
                const uint8_t *payload, uint16_t payloadLength) {
  uint8_t frame[8 + kMaxPayload + 3 + 2] = {
      'O', '1', kVersion, type, 0, 0, 0, 0};
  writeLe16(frame + 4, sequence);
  writeLe16(frame + 6, payloadLength);
  if (payloadLength != 0) memcpy(frame + 8, payload, payloadLength);

  uint16_t crc = crc16(frame, 8);
  for (uint16_t i = 0; i < payloadLength; ++i) {
    crc ^= static_cast<uint16_t>(payload[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  writeLe16(frame + 8 + payloadLength, crc);
  Serial.write(frame, static_cast<size_t>(10 + payloadLength));
}

void respondStatus(uint8_t requestType, uint16_t sequence, uint8_t status) {
  writeFrame(requestType | kResponseBit, sequence, &status, 1);
}

void handleRequest(uint8_t type, uint16_t sequence,
                   const uint8_t *payload, uint16_t payloadLength) {
  uint8_t response[kMaxPayload + 3];
  lastValidRequestMs = millis();

  if (type == kTypePing) {
    if (payloadLength != 0) {
      respondStatus(type, sequence, kStatusInvalidPayload);
      return;
    }
    const uint8_t pong[] = {kStatusOk, kVersion, 'R', 'A', '4'};
    writeFrame(type | kResponseBit, sequence, pong, sizeof(pong));
    return;
  }

  if (type == kTypeGetStatus) {
    if (payloadLength != 0) {
      respondStatus(type, sequence, kStatusInvalidPayload);
      return;
    }
    response[0] = kStatusOk;
    response[1] = digitalRead(kSensePin) ? 1 : 0;
    response[2] = highAssistActive ? 1 : 0;
    writeLe32(response + 3, exchangeCount);
    writeLe32(response + 7, framingErrorCount);
    writeFrame(type | kResponseBit, sequence, response, 11);
    return;
  }

  if (type == kTypeRelease) {
    if (payloadLength != 0) {
      respondStatus(type, sequence, kStatusInvalidPayload);
      return;
    }
    highAssistActive = false;
    releaseLine();
    drainCameraRx();
    respondStatus(type, sequence, kStatusOk);
    return;
  }

  if (type != kTypeExchange) {
    respondStatus(type, sequence, kStatusUnknownType);
    return;
  }

  if (payloadLength < 8) {
    respondStatus(type, sequence, kStatusInvalidPayload);
    return;
  }

  const uint16_t expected = readLe16(payload);
  const uint16_t firstTimeout = readLe16(payload + 2);
  const uint16_t interByteTimeout = readLe16(payload + 4);
  const uint16_t transmitLength = readLe16(payload + 6);
  if (expected > kMaxPayload || transmitLength > kMaxCameraTx ||
      payloadLength != static_cast<uint16_t>(8 + transmitLength)) {
    respondStatus(type, sequence, kStatusLimitExceeded);
    return;
  }
  if ((expected != 0 && (firstTimeout == 0 || firstTimeout > 5000 ||
                         interByteTimeout == 0 || interByteTimeout > 5000))) {
    respondStatus(type, sequence, kStatusInvalidPayload);
    return;
  }

  ensureCameraHardware();
  drainCameraRx();
  if (!cycleCounterStarted) beginCycleCounter();
  for (uint16_t i = 0; i < transmitLength; ++i) sendCameraByte(payload[8 + i]);
  const size_t received = receiveCamera(response + 3, expected,
                                        firstTimeout, interByteTimeout);
  ++exchangeCount;
  response[0] = received == expected ? kStatusOk : kStatusCameraTimeout;
  writeLe16(response + 1, static_cast<uint16_t>(received));
  writeFrame(type | kResponseBit, sequence, response,
             static_cast<uint16_t>(3 + received));
}

enum class ParserState : uint8_t { MagicO, Magic1, Header, Payload, Crc };
ParserState parserState = ParserState::MagicO;
uint8_t header[8];
uint8_t payload[kMaxPayload];
uint8_t crcBytes[2];
uint16_t parserIndex = 0;
uint16_t payloadLength = 0;

void resetParser() {
  parserState = ParserState::MagicO;
  parserIndex = 0;
  payloadLength = 0;
}

void consumeHostByte(uint8_t value) {
  switch (parserState) {
    case ParserState::MagicO:
      if (value == 'O') {
        header[0] = value;
        parserState = ParserState::Magic1;
      }
      break;
    case ParserState::Magic1:
      if (value == '1') {
        header[1] = value;
        parserIndex = 2;
        parserState = ParserState::Header;
      } else {
        parserState = value == 'O' ? ParserState::Magic1 : ParserState::MagicO;
      }
      break;
    case ParserState::Header:
      header[parserIndex++] = value;
      if (parserIndex == sizeof(header)) {
        payloadLength = readLe16(header + 6);
        if (header[2] != kVersion || payloadLength > kMaxPayload) {
          ++framingErrorCount;
          respondStatus(header[3], readLe16(header + 4), kStatusInvalidPayload);
          resetParser();
        } else {
          parserIndex = 0;
          parserState = payloadLength == 0 ? ParserState::Crc : ParserState::Payload;
        }
      }
      break;
    case ParserState::Payload:
      payload[parserIndex++] = value;
      if (parserIndex == payloadLength) {
        parserIndex = 0;
        parserState = ParserState::Crc;
      }
      break;
    case ParserState::Crc:
      crcBytes[parserIndex++] = value;
      if (parserIndex == sizeof(crcBytes)) {
        uint16_t calculated = crc16(header, sizeof(header));
        for (uint16_t i = 0; i < payloadLength; ++i) {
          calculated ^= static_cast<uint16_t>(payload[i]) << 8;
          for (uint8_t bit = 0; bit < 8; ++bit) {
            calculated = (calculated & 0x8000)
                             ? static_cast<uint16_t>((calculated << 1) ^ 0x1021)
                             : static_cast<uint16_t>(calculated << 1);
          }
        }
        const uint16_t receivedCrc = readLe16(crcBytes);
        if (calculated == receivedCrc) {
          handleRequest(header[3], readLe16(header + 4), payload, payloadLength);
        } else {
          ++framingErrorCount;
          respondStatus(header[3], readLe16(header + 4), kStatusCrcError);
        }
        resetParser();
      }
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(kHostBaud);
  lastValidRequestMs = millis();
}

void loop() {
  while (Serial.available()) consumeHostByte(static_cast<uint8_t>(Serial.read()));
  if (highAssistActive &&
      static_cast<uint32_t>(millis() - lastValidRequestMs) >= kDriverIdleReleaseMs) {
    highAssistActive = false;
    releaseLine();
    drainCameraRx();
  }
}

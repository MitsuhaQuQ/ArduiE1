#include <Arduino.h>

extern "C" {
#include "tusb.h"
}

// Experimental UNO R4 Minima emulation of the original ES-E1 USB transport.
// The companion build script replaces the normal CDC descriptors with one
// KLSI/MCCI-style vendor interface (bulk OUT 0x02, bulk IN 0x81).
//
// Camera-side circuit:
// D4 -> 10k -> NPN base, base -> 100k -> emitter, emitter -> COMMON,
// collector -> 330R -> DATA-A.
// D5 -> 1k -> 1N4148 anode, diode cathode/stripe -> DATA-A.
// DATA-B -> 5.1k -> D0/RX. D1/D2 remain disconnected.

namespace {

constexpr uint8_t kLowDrivePin = 4;
constexpr uint8_t kHighAssistPin = 5;
constexpr uint32_t kCpuHz = 48000000UL;
constexpr uint32_t kCyclesPerBit = kCpuHz / 9600UL;
constexpr size_t kUsbBlockSize = 64;
constexpr size_t kUsbPayloadSize = 62;
constexpr uint32_t kCameraQuietUs = 1800;

uint8_t usbOutBlock[kUsbBlockSize];
size_t usbOutUsed = 0;
uint8_t cameraRx[kUsbPayloadSize];
size_t cameraRxUsed = 0;
uint32_t lastCameraByteUs = 0;
bool readChannelEnabled = false;
bool highAssistActive = false;
bool cycleCounterStarted = false;

// The five bytes observed in original ES-E1 request 1 traffic.
volatile uint8_t serialConfig[5] = {5, 6, 8, 0, 0};
uint8_t pendingConfig[5] = {5, 6, 8, 0, 0};

uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(p[1] << 8);
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

void processUsbBlock(const uint8_t* block) {
  const uint16_t length = readLe16(block);
  if (length > kUsbPayloadSize) return;
  for (uint16_t i = 0; i < length; ++i) sendCameraByte(block[2 + i]);
}

void pollUsbOut() {
  while (tud_vendor_available()) {
    usbOutUsed += tud_vendor_read(usbOutBlock + usbOutUsed,
                                  kUsbBlockSize - usbOutUsed);
    if (usbOutUsed == kUsbBlockSize) {
      processUsbBlock(usbOutBlock);
      usbOutUsed = 0;
    }
  }
}

void sendCameraBlock() {
  if (!readChannelEnabled || cameraRxUsed == 0 || !tud_vendor_mounted()) return;
  if (tud_vendor_write_available() < kUsbBlockSize) return;

  uint8_t block[kUsbBlockSize] = {};
  block[0] = static_cast<uint8_t>(cameraRxUsed);
  block[1] = 0;
  memcpy(block + 2, cameraRx, cameraRxUsed);
  if (tud_vendor_write(block, sizeof(block)) == sizeof(block)) {
    tud_vendor_write_flush();
    cameraRxUsed = 0;
  }
}

void pollCameraIn() {
  while (Serial1.available() && cameraRxUsed < sizeof(cameraRx)) {
    cameraRx[cameraRxUsed++] = static_cast<uint8_t>(Serial1.read());
    lastCameraByteUs = micros();
  }
  if (cameraRxUsed == sizeof(cameraRx) ||
      (cameraRxUsed != 0 &&
       static_cast<uint32_t>(micros() - lastCameraByteUs) >= kCameraQuietUs)) {
    sendCameraBlock();
  }
}

}  // namespace

extern "C" bool tud_vendor_control_xfer_cb(
    uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
  if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR ||
      request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE ||
      request->bmRequestType_bit.direction != TUSB_DIR_OUT) {
    return false;
  }

  if (stage == CONTROL_STAGE_SETUP) {
    if (request->bRequest == 1 && request->wValue == 0 &&
        request->wLength == sizeof(pendingConfig)) {
      return tud_control_xfer(rhport, request, pendingConfig,
                              sizeof(pendingConfig));
    }
    if (request->bRequest == 3 && request->wLength == 0 &&
        (request->wValue == 2 || request->wValue == 3)) {
      return tud_control_status(rhport, request);
    }
    return false;
  }

  if (stage == CONTROL_STAGE_ACK) {
    if (request->bRequest == 1) {
      for (size_t i = 0; i < sizeof(pendingConfig); ++i) {
        serialConfig[i] = pendingConfig[i];
      }
    } else if (request->bRequest == 3) {
      readChannelEnabled = request->wValue == 3;
      if (!readChannelEnabled) cameraRxUsed = 0;
    }
  }
  return true;
}

void setup() {
  pinMode(kLowDrivePin, OUTPUT);
  pinMode(kHighAssistPin, OUTPUT);
  releaseLine();
  Serial1.begin(9600, SERIAL_8N1);
  beginCycleCounter();
}

void loop() {
  pollUsbOut();
  pollCameraIn();
}

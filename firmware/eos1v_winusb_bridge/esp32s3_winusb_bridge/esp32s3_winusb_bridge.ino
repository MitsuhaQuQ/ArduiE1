// UNUSABLE EXPERIMENTAL DRAFT.
// A successful compile does not prove correct UNO R4 WiFi boot, USB, UART, or
// recovery behavior. Do not flash this sketch in its current repository state.
#include <Arduino.h>

#ifndef ARDUINO_USB_MODE
#error This sketch requires an ESP32-S3 with native USB OTG.
#elif ARDUINO_USB_MODE == 1
#error Select USB-OTG (TinyUSB), ARDUINO_USB_MODE=0.
#else

#include "USB.h"
#include "esp32-hal-tinyusb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {

constexpr uint32_t kRa4Baud = 115200;
constexpr size_t kChunk = 64;
constexpr int kRa4RxPin = 44;
constexpr int kRa4TxPin = 43;
// The UNO R4 WiFi omits pull-ups on the RA4 mode/reset controls. The official
// bridge actively holds both high; leaving them floating can prevent the RA4
// application from running even though the S3 USB interface is healthy.
constexpr int kRa4BootPin = 9;
constexpr int kRa4ResetPin = 4;

QueueHandle_t usbRxQueue = nullptr;
uint8_t vendorInterface = 0;
volatile uint32_t usbRxBytes = 0;
volatile uint32_t uartTxBytes = 0;
volatile uint32_t uartRxBytes = 0;
volatile uint32_t queueDroppedBytes = 0;
volatile uint32_t forwardedFrames = 0;
volatile uint32_t usbTxAcceptedBytes = 0;
volatile uint32_t usbTxZeroWrites = 0;
bool respondToLocalRequest(const uint8_t *data, size_t length);

uint16_t loadVendorDescriptor(uint8_t *destination, uint8_t *interfaceNumber) {
  vendorInterface = *interfaceNumber;
  const uint8_t stringIndex = tinyusb_add_string_descriptor("Open1V WinUSB");
  const uint8_t endpoint = tinyusb_get_free_duplex_endpoint();
  if (endpoint == 0) return 0;
  const uint8_t descriptor[TUD_VENDOR_DESC_LEN] = {
      TUD_VENDOR_DESCRIPTOR(*interfaceNumber, stringIndex, endpoint,
                            static_cast<uint8_t>(0x80 | endpoint), kChunk)};
  ++(*interfaceNumber);
  memcpy(destination, descriptor, sizeof(descriptor));
  return sizeof(descriptor);
}

extern "C" void tud_vendor_rx_cb(uint8_t interfaceNumber,
                                 const uint8_t *buffer, uint32_t size) {
  uint8_t fifoBuffer[kChunk];
  if (buffer == nullptr) {
    while (tud_vendor_n_available(interfaceNumber)) {
      const uint32_t read = tud_vendor_n_read(interfaceNumber, fifoBuffer,
                                              sizeof(fifoBuffer));
      usbRxBytes += read;
      if (respondToLocalRequest(fifoBuffer, read)) continue;
      for (uint32_t i = 0; i < read; ++i) {
        if (xQueueSend(usbRxQueue, fifoBuffer + i, 0) != pdTRUE) {
          ++queueDroppedBytes;
        }
      }
    }
    return;
  }
  usbRxBytes += size;
  if (respondToLocalRequest(buffer, size)) return;
  for (uint32_t i = 0; i < size; ++i) {
    if (xQueueSend(usbRxQueue, buffer + i, 0) != pdTRUE) ++queueDroppedBytes;
  }
}

size_t writeUsb(const uint8_t *data, size_t length) {
  size_t offset = 0;
  while (offset < length) {
    const uint32_t written = tud_vendor_n_write(
        vendorInterface, data + offset, static_cast<uint32_t>(length - offset));
    // Never block the Arduino loop waiting for the host to post an IN read.
    // A blocked loop would also stop draining the OUT endpoint.
    if (written == 0) break;
    usbTxAcceptedBytes += written;
    offset += written;
  }
  if (offset < length) ++usbTxZeroWrites;
  tud_vendor_n_write_flush(vendorInterface);
  return offset;
}

uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void storeLe32(uint8_t *destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
  destination[2] = static_cast<uint8_t>(value >> 16);
  destination[3] = static_cast<uint8_t>(value >> 24);
}

bool respondToLocalRequest(const uint8_t *data, size_t length) {
  if (length != 10 || data[0] != 'O' || data[1] != '1' || data[2] != 1 ||
      data[6] != 0 || data[7] != 0) return false;
  const uint16_t received = static_cast<uint16_t>(data[8]) |
                            static_cast<uint16_t>(data[9] << 8);
  if (crc16(data, 8) != received) return false;
  if (data[3] == 0x01) {
    uint8_t response[] = {'O', '1', 1, 0x81, data[4], data[5], 4, 0,
                          0, 1, 'S', '3', 0, 0};
    const uint16_t checksum = crc16(response, sizeof(response) - 2);
    response[sizeof(response) - 2] = static_cast<uint8_t>(checksum);
    response[sizeof(response) - 1] = static_cast<uint8_t>(checksum >> 8);
    writeUsb(response, sizeof(response));
    return true;
  }
  if (data[3] == 0x03) {
    uint8_t response[39] = {'O', '1', 1, 0x83, data[4], data[5], 29, 0, 0};
    storeLe32(response + 9, usbRxBytes);
    storeLe32(response + 13, uartTxBytes);
    storeLe32(response + 17, uartRxBytes);
    storeLe32(response + 21, queueDroppedBytes);
    storeLe32(response + 25, forwardedFrames);
    storeLe32(response + 29, usbTxAcceptedBytes);
    storeLe32(response + 33, usbTxZeroWrites);
    const uint16_t checksum = crc16(response, sizeof(response) - 2);
    response[sizeof(response) - 2] = static_cast<uint8_t>(checksum);
    response[sizeof(response) - 1] = static_cast<uint8_t>(checksum >> 8);
    writeUsb(response, sizeof(response));
    return true;
  }
  return false;
}

uint8_t usbFrame[522];
size_t usbFrameUsed = 0;
size_t usbFrameExpected = 0;

void acceptUsbByte(uint8_t value) {
  if (usbFrameUsed == 0 && value != 'O') return;
  if (usbFrameUsed == 1 && value != '1') {
    usbFrameUsed = value == 'O' ? 1 : 0;
    usbFrame[0] = value;
    return;
  }
  usbFrame[usbFrameUsed++] = value;
  if (usbFrameUsed == 8) {
    const size_t payloadLength = static_cast<size_t>(usbFrame[6]) |
                                 (static_cast<size_t>(usbFrame[7]) << 8);
    usbFrameExpected = 10 + payloadLength;
    if (usbFrameExpected > sizeof(usbFrame)) {
      usbFrameUsed = 0;
      usbFrameExpected = 0;
    }
  }
  if (usbFrameExpected != 0 && usbFrameUsed == usbFrameExpected) {
    if (!respondToLocalRequest(usbFrame, usbFrameUsed)) {
      uartTxBytes += Serial.write(usbFrame, usbFrameUsed);
      ++forwardedFrames;
    }
    usbFrameUsed = 0;
    usbFrameExpected = 0;
  }
}

void forwardUsbToRa4() {
  uint8_t buffer[kChunk];
  size_t used = 0;
  while (used < sizeof(buffer) && xQueueReceive(usbRxQueue, buffer + used, 0)) ++used;
  for (size_t i = 0; i < used; ++i) acceptUsbByte(buffer[i]);
}

void forwardRa4ToUsb() {
  static uint8_t pending[kChunk];
  static size_t pendingOffset = 0;
  static size_t pendingLength = 0;
  if (pendingOffset < pendingLength) {
    pendingOffset += writeUsb(pending + pendingOffset,
                              pendingLength - pendingOffset);
    if (pendingOffset < pendingLength) return;
    pendingOffset = 0;
    pendingLength = 0;
  }
  while (Serial.available()) {
    const size_t available = Serial.available();
    const size_t wanted = available < sizeof(pending) ? available : sizeof(pending);
    const size_t read = Serial.readBytes(pending, wanted);
    if (read == 0) break;
    uartRxBytes += read;
    pendingLength = read;
    pendingOffset = writeUsb(pending, pendingLength);
    if (pendingOffset < pendingLength) return;
    pendingOffset = 0;
    pendingLength = 0;
  }
}

}  // namespace

void setup() {
  pinMode(kRa4BootPin, OUTPUT);
  pinMode(kRa4ResetPin, OUTPUT);
  digitalWrite(kRa4BootPin, HIGH);
  digitalWrite(kRa4ResetPin, HIGH);

  // On the UNO R4 WiFi ESP32-S3, UART0 is the internal link to the RA4M1.
  // Use explicit pins, matching Arduino's official UNO R4 WiFi USB bridge.
  Serial.setRxBufferSize(2048);
  Serial.setTxBufferSize(2048);
  Serial.begin(kRa4Baud, SERIAL_8N1, kRa4RxPin, kRa4TxPin);
  while (Serial.available()) Serial.read();

  usbRxQueue = xQueueCreate(2048, sizeof(uint8_t));
  if (!usbRxQueue) while (true) delay(1000);
  if (tinyusb_enable_interface(USB_INTERFACE_VENDOR, TUD_VENDOR_DESC_LEN,
                               loadVendorDescriptor) != ESP_OK) {
    while (true) delay(1000);
  }

  // Do not set a project-owned VID/PID until identifiers have been allocated.
  USB.productName("Open1V Camera Bridge Prototype");
  USB.manufacturerName("Open1V Community Project");
  // Arduino-ESP32 3.3.x serves both WebUSB and Microsoft OS 2.0 requests
  // behind this switch. The latter advertises WINUSB as the compatible ID,
  // allowing modern Windows to bind WinUSB without a custom INF.
  USB.webUSB(true);
  USB.begin();
}

void loop() {
  forwardUsbToRa4();
  forwardRa4ToUsb();
  delay(1);
}

#endif

#include <Arduino.h>

// EOS-1V active push-pull UART test for UNO R4 WiFi.
// Remove the NPN/D4 circuit completely for this test.
// D1 / Serial1 TX -- 1k -- EOS FOCUS / DATA-A
// D0 / Serial1 RX -- 5.1k -- EOS SHUTTER / DATA-B
// UNO GND -- EOS COMMON

static size_t receiveQuiet(uint8_t *buffer, size_t count, uint32_t timeoutMs) {
  size_t used = 0;
  const uint32_t deadline = millis() + timeoutMs;
  while (used < count && static_cast<int32_t>(millis() - deadline) < 0) {
    if (Serial1.available()) buffer[used++] = static_cast<uint8_t>(Serial1.read());
  }
  return used;
}

static void drainRx() {
  while (Serial1.available()) Serial1.read();
}

static void sendByte(uint8_t value) {
  Serial1.write(value);
  Serial1.flush();
}

static void printHex(uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

static void printPacket(const char *label, const uint8_t *data,
                        size_t used, size_t expected) {
  Serial.print(label);
  Serial.print(" (");
  Serial.print(used);
  Serial.print('/');
  Serial.print(expected);
  Serial.print("):");
  for (size_t i = 0; i < used; ++i) {
    Serial.print(' ');
    printHex(data[i]);
  }
  Serial.println();
}

static bool cleanExit() {
  uint8_t reply1[1];
  uint8_t reply2[1];
  drainRx();
  sendByte(0xF2);
  const size_t used1 = receiveQuiet(reply1, 1, 500);
  if (used1 != 1 || reply1[0] != 0xF4) {
    printPacket("EXIT RX1", reply1, used1, 1);
    return false;
  }
  sendByte(0xF4);
  delay(305);
  sendByte(0xF2);
  const size_t used2 = receiveQuiet(reply2, 1, 500);
  printPacket("EXIT RX1", reply1, used1, 1);
  printPacket("EXIT RX2", reply2, used2, 1);
  return used2 == 1 && reply2[0] == 0xF2;
}

static void runF1Test() {
  uint8_t ffReply[1];
  uint8_t f1Reply[6];
  drainRx();
  Serial.println("Running hardware-UART FF/F4/F1 test");

  sendByte(0xFF);
  const size_t ffUsed = receiveQuiet(ffReply, 1, 500);
  if (ffUsed != 1 || ffReply[0] != 0xF4) {
    printPacket("FF RX", ffReply, ffUsed, 1);
    Serial.println("STOP: initial probe failed");
    return;
  }

  sendByte(0xF4);
  delay(63);
  sendByte(0xF1);
  const size_t f1Used = receiveQuiet(f1Reply, 6, 500);

  printPacket("FF RX", ffReply, ffUsed, 1);
  printPacket("F1 RX", f1Reply, f1Used, 6);
  if (f1Used == 6 && f1Reply[0] == 0xF1) {
    Serial.println("HARDWARE UART TEST OK");
    if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
    else Serial.println("EXIT FAILED");
  } else {
    Serial.println("STOP: F1 reply failed; TX is idle HIGH");
  }
}

void setup() {
  Serial1.begin(9600, SERIAL_8N1); // D1 actively idles HIGH
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("EOS-1V hardware UART test ready");
  Serial.println("Confirm PC mode, then send q exactly once");
}

void loop() {
  while (Serial.available()) {
    const char command = Serial.read();
    if (command == 'q' || command == 'Q') runF1Test();
  }
}

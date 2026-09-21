#include <Arduino.h>

// EOS-1V isolated adaptive UART driver.
// LOW path:  D4 --10k-- NPN B, B--100k--E, E--COMMON, C--330R--DATA-A
// HIGH assist: D5 --1k-- 1N4007 anode; cathode/stripe -- DATA-A
// RX path:   DATA-B --5.1k-- D0/RX
// D1/D2 disconnected. Both control pins idle LOW. After a long LOW run, the
// isolated D5 path stays active for the complete following HIGH run.

constexpr uint8_t LOW_DRIVE_PIN = 4;
constexpr uint8_t HIGH_KICK_PIN = 5;
constexpr uint8_t SENSE_PIN = 2;
constexpr uint32_t CPU_HZ = 48000000UL;
constexpr uint32_t CYCLES_PER_BIT = CPU_HZ / 9600UL;
constexpr uint8_t MAX_FILM_ROLLS = 100;
constexpr uint8_t MAX_RECORDS_PER_ROLL = 40;
uint8_t lastSamples[10] = {};
bool activeHighMode = false;
bool filmDeleteArmed = false;
uint32_t filmDeleteDeadline = 0;

static void beginCycleCounter() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline void pullLow() {
  digitalWrite(HIGH_KICK_PIN, LOW);
  digitalWrite(LOW_DRIVE_PIN, HIGH); // NPN on
}

static inline void releaseLine() {
  digitalWrite(LOW_DRIVE_PIN, LOW);  // NPN off
  digitalWrite(HIGH_KICK_PIN, LOW);  // diode isolates high driver
}

static void enterHigh(uint8_t precedingLowSlots) {
  digitalWrite(LOW_DRIVE_PIN, LOW);  // always release LOW driver first
  if (activeHighMode || precedingLowSlots >= 2) {
    activeHighMode = true;
    digitalWrite(HIGH_KICK_PIN, HIGH);
  } else {
    digitalWrite(HIGH_KICK_PIN, LOW);
  }
}

static void sendByte(uint8_t value) {
  pullLow();
  const uint32_t frameStart = DWT->CYCCNT;
  uint8_t lowSlots = 1;
  noInterrupts();

  for (uint8_t slot = 0; slot < 10; ++slot) {
    const uint32_t midpoint = frameStart + slot * CYCLES_PER_BIT
                            + CYCLES_PER_BIT / 2;
    const uint32_t boundary = frameStart + (slot + 1) * CYCLES_PER_BIT;
    while (static_cast<int32_t>(DWT->CYCCNT - midpoint) < 0) {}
    lastSamples[slot] = digitalRead(SENSE_PIN) ? 1 : 0;
    while (static_cast<int32_t>(DWT->CYCCNT - boundary) < 0) {}

    if (slot < 8) {
      const uint8_t bit = slot;
      if (value & (1u << bit)) {
        if (lowSlots) enterHigh(lowSlots);
        lowSlots = 0;
      } else {
        if (!lowSlots) pullLow();
        ++lowSlots;
      }
    } else if (slot == 8) {
      if (lowSlots) enterHigh(lowSlots); // begin stop bit
      lowSlots = 0;
    } else {
      // Once the camera pull-up has dropped out, keep the isolated HIGH
      // driver active through inter-byte idle. The next LOW disables it first.
      if (activeHighMode) {
        digitalWrite(LOW_DRIVE_PIN, LOW);
        digitalWrite(HIGH_KICK_PIN, HIGH);
      } else {
        releaseLine();
      }
    }
  }
  interrupts();
}

static void copySamples(uint8_t *destination) {
  memcpy(destination, lastSamples, sizeof(lastSamples));
}

static void printSamples(const char *label, const uint8_t *samples) {
  Serial.print(label);
  Serial.print(" DATA-A bits: ");
  for (uint8_t i = 0; i < 10; ++i) Serial.print(samples[i]);
  Serial.println();
}

static bool packetPayloadChecksumValid(const uint8_t *packet, size_t used);

static size_t receiveQuiet(uint8_t *data, size_t count, uint32_t timeoutMs) {
  size_t used = 0;
  const uint32_t deadline = millis() + timeoutMs;
  while (used < count && static_cast<int32_t>(millis() - deadline) < 0) {
    if (Serial1.available()) data[used++] = static_cast<uint8_t>(Serial1.read());
  }
  return used;
}

static size_t commandWithF4Retry(uint8_t command, uint8_t *data,
                                 size_t expected, uint32_t timeoutMs) {
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    sendByte(command);
    size_t used = receiveQuiet(data, 1, timeoutMs);
    if (used != 1) return used;
    if (data[0] == 0xF4) {
      sendByte(0xF4);
      delay(2);
      continue;
    }
    if (data[0] != command) return used;
    if (expected > 1) used += receiveQuiet(data + 1, expected - 1, timeoutMs);
    if (used == expected && expected > 1 && !packetPayloadChecksumValid(data, used)) {
      Serial.println("PACKET REJECTED: invalid length byte or checksum");
      return 0;
    }
    return used;
  }
  return 0;
}

static size_t variablePacketWithF4Retry(uint8_t command, uint8_t *data,
                                        size_t capacity, uint32_t timeoutMs) {
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    sendByte(command);
    size_t used = receiveQuiet(data, 1, timeoutMs);
    if (used != 1) return used;
    if (data[0] == 0xF4) {
      sendByte(0xF4);
      delay(2);
      continue;
    }
    if (data[0] != command) return used;
    used += receiveQuiet(data + 1, 1, timeoutMs);
    if (used != 2) return used;
    const size_t total = static_cast<size_t>(data[1]) + 3;
    if (total > capacity) return used;
    used += receiveQuiet(data + 2, total - 2, timeoutMs);
    return used;
  }
  return 0;
}

// E3 is read-only, but a successful call advances the camera's roll iterator.
// Retry only when absolutely no reply byte arrived; never retry a partial packet.
static size_t readE3WithZeroReplyRetry(uint8_t *data, size_t capacity) {
  size_t used = 0;
  for (uint8_t attempt = 1; attempt <= 3; ++attempt) {
    delay(attempt == 1 ? 78 : (attempt == 2 ? 250 : 500));
    used = variablePacketWithF4Retry(0xE3, data, capacity, 1200);
    if (used != 0) return used;
    if (attempt < 3) {
      Serial.print("E3 zero-byte reply; retrying read-only command, attempt ");
      Serial.println(attempt + 1);
    }
    drainRx();
  }
  return used;
}

static void drainRx() { while (Serial1.available()) Serial1.read(); }
static void printHex(uint8_t v) { if (v < 0x10) Serial.print('0'); Serial.print(v, HEX); }
static void printPacket(const char *name, const uint8_t *p, size_t n, size_t expected) {
  Serial.print(name); Serial.print(" ("); Serial.print(n); Serial.print('/');
  Serial.print(expected); Serial.print("):");
  for (size_t i = 0; i < n; ++i) { Serial.print(' '); printHex(p[i]); }
  Serial.println();
}

static bool packetPayloadChecksumValid(const uint8_t *packet, size_t used) {
  if (used < 3 || used != static_cast<size_t>(packet[1]) + 3) return false;
  uint8_t checksum = 0;
  for (size_t i = 2; i + 1 < used; ++i) checksum = static_cast<uint8_t>(checksum + packet[i]);
  return checksum == packet[used - 1];
}

static bool printCameraIdFromF1(const uint8_t *packet, size_t used) {
  if (used != 6 || packet[0] != 0xF1 || packet[1] != 3) {
    Serial.println("CAMERA ID READ FAILED: invalid F1 packet");
    return false;
  }
  const uint8_t checksum = static_cast<uint8_t>(packet[2] + packet[3] + packet[4]);
  if (checksum != packet[5]) {
    Serial.print("CAMERA ID READ FAILED: checksum expected ");
    printHex(checksum);
    Serial.print(", received ");
    printHex(packet[5]);
    Serial.println();
    return false;
  }
  Serial.print("CAMERA ID: ");
  Serial.print(packet[3] & 0x7F, DEC);
  Serial.print(" (wire byte ");
  printHex(packet[3]);
  Serial.println(")");
  Serial.print("F1 camera type: ");
  printHex(packet[2]);
  Serial.print(", status: ");
  printHex(packet[4]);
  Serial.println();
  Serial.println("CAMERA ID READ OK; no settings were written");
  return true;
}

static bool cleanExit() {
  uint8_t a[1], b[1];
  drainRx();
  sendByte(0xF2);
  const size_t na = receiveQuiet(a, 1, 500);
  if (na == 1 && a[0] == 0xF2) {
    printPacket("EXIT RX1", a, na, 1);
    Serial.println("Single-stage F2/F2 exit accepted");
    activeHighMode = false;
    releaseLine();
    return true;
  }
  if (na != 1 || a[0] != 0xF4) {
    printPacket("EXIT RX1", a, na, 1);
    activeHighMode = false;
    releaseLine();
    return false;
  }
  sendByte(0xF4);
  delay(305);
  sendByte(0xF2);
  const size_t nb = receiveQuiet(b, 1, 500);
  printPacket("EXIT RX1", a, na, 1);
  printPacket("EXIT RX2", b, nb, 1);
  const bool ok = nb == 1 && b[0] == 0xF2;
  activeHighMode = false;
  releaseLine();
  return ok;
}

static void runTest() {
  uint8_t ff[1], f1[6];
  drainRx();
  activeHighMode = false;
  releaseLine();
  Serial.println("Running isolated dual-driver FF/F4/F1 test");
  sendByte(0xFF);
  const size_t nff = receiveQuiet(ff, 1, 500);
  if (nff != 1 || ff[0] != 0xF4) {
    printPacket("FF RX", ff, nff, 1);
    activeHighMode = false;
    releaseLine();
    Serial.println("STOP: initial probe failed; both drivers released");
    return;
  }
  sendByte(0xF4);
  delay(63);
  sendByte(0xF1);
  const size_t nf1 = receiveQuiet(f1, 6, 500);
  printPacket("FF RX", ff, nff, 1);
  printPacket("F1 RX", f1, nf1, 6);
  if (nf1 == 6 && f1[0] == 0xF1) {
    Serial.println("DUAL-DRIVER TEST OK");
    if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
    else Serial.println("EXIT FAILED; both drivers released");
  } else {
    activeHighMode = false;
    releaseLine();
    Serial.println("STOP: F1 reply failed; both drivers released");
  }
}

static void runFullHandshake(uint32_t f4ToF6DelayMs) {
  uint8_t ff[1], f6[17], f1[6];
  uint8_t ffBits[10], f4Bits[10], f6Bits[10];
  drainRx();
  activeHighMode = false;
  releaseLine();
  Serial.print("Running isolated dual-driver full handshake; F4-to-F6 delay ");
  Serial.print(f4ToF6DelayMs);
  Serial.println(" ms");

  sendByte(0xFF);
  copySamples(ffBits);
  const size_t nff = receiveQuiet(ff, 1, 500);
  if (nff != 1 || ff[0] != 0xF4) {
    printPacket("FF RX", ff, nff, 1);
    activeHighMode = false;
    releaseLine();
    Serial.println("STOP: initial probe failed; both drivers released");
    return;
  }

  sendByte(0xF4);
  copySamples(f4Bits);
  if (f4ToF6DelayMs) delay(f4ToF6DelayMs);
  sendByte(0xF6);
  copySamples(f6Bits);
  const size_t nf6 = receiveQuiet(f6, 17, 500);
  if (nf6 != 17 || f6[0] != 0xF6) {
    printPacket("FF RX", ff, nff, 1);
    printPacket("F6 RX", f6, nf6, 17);
    printSamples("FF", ffBits);
    printSamples("F4", f4Bits);
    printSamples("F6", f6Bits);
    activeHighMode = false;
    releaseLine();
    Serial.println("STOP: F6 reply failed; both drivers released");
    return;
  }

  sendByte(0xF1);
  const size_t nf1 = receiveQuiet(f1, 6, 500);
  printPacket("FF RX", ff, nff, 1);
  printPacket("F6 RX", f6, nf6, 17);
  printPacket("F1 RX", f1, nf1, 6);
  if (nf1 == 6 && f1[0] == 0xF1) {
    Serial.println("FULL HANDSHAKE OK");
    printCameraIdFromF1(f1, nf1);
    if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
    else Serial.println("EXIT FAILED; both drivers released");
  } else {
    activeHighMode = false;
    releaseLine();
    Serial.println("STOP: F1 reply failed after valid F6; both drivers released");
  }
}

static void runReadSettingsFlow(uint8_t filmMode) {
  uint8_t ff1[1], f6a[17], f1a[6];
  uint8_t sync[1], f6b[17];
  uint8_t ff2[1], f1b[6], e8[11], fc[5], e1[5];
  uint8_t ff3[1], f1c[6], sync2[1], f6c[17], e1c[5], e3[36];
  size_t nff1 = 0, nf6a = 0, nf1a = 0, nsync = 0, nf6b = 0;
  size_t nff2 = 0, nf1b = 0, ne8 = 0, nfc = 0, ne1 = 0;
  size_t nff3 = 0, nf1c = 0, nsync2 = 0, nf6c = 0, ne1c = 0, ne3 = 0;
  const char *stage = "initial FF";
  bool sessionEstablished = false;
  bool filmEmpty = false;

  drainRx();
  activeHighMode = false;
  releaseLine();
  Serial.println("Running captured read-settings flow; output resumes when complete");

  // First physical session establishment.
  sendByte(0xFF);
  nff1 = receiveQuiet(ff1, 1, 500);
  if (nff1 != 1 || ff1[0] != 0xF4) goto failed;
  stage = "initial F6";
  sendByte(0xF4);
  delay(300);
  sendByte(0xF6);
  nf6a = receiveQuiet(f6a, 17, 500);
  if (nf6a != 17 || f6a[0] != 0xF6) goto failed;
  stage = "initial F1";
  nf1a = commandWithF4Retry(0xF1, f1a, 6, 500);
  if (nf1a != 6 || f1a[0] != 0xF1) goto failed;
  sessionEstablished = true;

  // Camera-originated F4 synchronization observed about 200 ms later.
  stage = "camera F4 sync";
  nsync = receiveQuiet(sync, 1, 1500);
  nf6b = 0;
  if (nsync == 1) {
    if (sync[0] != 0xF4) goto failed;
    stage = "sync F6";
    sendByte(0xF4);
    delay(2);
    sendByte(0xF6);
    nf6b = receiveQuiet(f6b, 17, 500);
    if (nf6b != 17 || f6b[0] != 0xF6) goto failed;
  }

  // Original application begins its read-settings logical connection later.
  stage = "settings FF";
  delay(1200);
  sendByte(0xFF);
  nff2 = receiveQuiet(ff2, 1, 500);
  if (nff2 != 1 || ff2[0] != 0xF4) goto failed;
  stage = "settings F1";
  sendByte(0xF4);
  delay(63);
  nf1b = commandWithF4Retry(0xF1, f1b, 6, 500);
  if (nf1b != 6 || f1b[0] != 0xF1) goto failed;

  stage = "E8";
  delay(78);
  ne8 = commandWithF4Retry(0xE8, e8, 11, 500);
  if (ne8 != 11 || e8[0] != 0xE8) goto failed;

  stage = "FC";
  delay(78);
  nfc = commandWithF4Retry(0xFC, fc, 5, 500);
  if (nfc != 5 || fc[0] != 0xFC) goto failed;

  stage = "E1";
  delay(78);
  ne1 = commandWithF4Retry(0xE1, e1, 5, 500);
  if (ne1 != 5 || e1[0] != 0xE1) goto failed;

  if (filmMode) {
    // Third logical connection used by the original film-download path.
    stage = "film FF";
    delay(1800);
    sendByte(0xFF);
    nff3 = receiveQuiet(ff3, 1, 500);
    if (nff3 != 1 || ff3[0] != 0xF4) goto failed;
    sendByte(0xF4);
    delay(63);
    stage = "film F1";
    nf1c = commandWithF4Retry(0xF1, f1c, 6, 500);
    if (nf1c != 6 || f1c[0] != 0xF1) goto failed;

    stage = "film F4 sync";
    nsync2 = receiveQuiet(sync2, 1, 1000);
    if (nsync2 != 1 || sync2[0] != 0xF4) goto failed;
    sendByte(0xF4);
    delay(2);
    stage = "film sync F6";
    sendByte(0xF6);
    nf6c = receiveQuiet(f6c, 17, 500);
    if (nf6c != 17 || f6c[0] != 0xF6) goto failed;

    // Any read command may collide with a camera-originated F4.
    stage = "film E1";
    delay(650);
    ne1c = commandWithF4Retry(0xE1, e1c, 5, 500);
    if (ne1c != 5 || e1c[0] != 0xE1) goto failed;

    const uint16_t reportedRolls = (uint16_t(e1c[2]) << 8) | e1c[3];
    filmEmpty = reportedRolls == 0;
    if (!filmEmpty) {
      stage = "E3 film header";
      ne3 = readE3WithZeroReplyRetry(e3, sizeof(e3));
      if (ne3 != 36 || e3[0] != 0xE3 || e3[1] != 0x21 ||
          !packetPayloadChecksumValid(e3, ne3)) goto failed;
    }
  }

  printPacket("SESSION F6", f6a, nf6a, 17);
  printPacket("SESSION F1", f1a, nf1a, 6);
  if (nf6b) printPacket("SYNC F6", f6b, nf6b, 17);
  else Serial.println("SYNC F4: not emitted; continued without optional refresh");
  printPacket("READ F1", f1b, nf1b, 6);
  printPacket("READ E8", e8, ne8, 11);
  printPacket("READ FC", fc, nfc, 5);
  printPacket("READ E1", e1, ne1, 5);
  if (filmMode) {
    printPacket("FILM E1", e1c, ne1c, 5);
    if (filmEmpty) {
      Serial.println("FILM DOWNLOAD READ OK: rolls=0 records=0; E3/E4 not sent");
    } else {
      printPacket("FILM E3 HEADER", e3, ne3, 36);
    }
    if (!filmEmpty && filmMode == 1) {
      Serial.println("FILM HEADER READ OK");
    } else if (!filmEmpty) {
      uint8_t packet[36];
      uint16_t totalRecords = 0;
      uint8_t rolls = 1;
      bool allDone = false;

      while (rolls <= MAX_FILM_ROLLS && !allDone) {
        bool rollDone = false;
        for (uint8_t record = 0; record < MAX_RECORDS_PER_ROLL; ++record) {
          delay(15);
          const size_t used = variablePacketWithF4Retry(0xE4, packet, sizeof(packet), 1000);
          if (used >= 4 && packet[0] == 0xE4 && packet[1] >= 2 &&
              packetPayloadChecksumValid(packet, used)) {
            ++totalRecords;
            printPacket("FILM E4 RECORD", packet, used, used);
          } else if (used == 4 && packet[1] == 0x01 && packet[2] == 0x00 && packet[3] == 0x00) {
            rollDone = true;
            Serial.println("FILM E4 ROLL END: E4 01 00 00");
            break;
          } else {
            Serial.println("FILM DOWNLOAD FAILED at E4");
            printPacket("PARTIAL E4", packet, used, 36);
            if (cleanExit()) Serial.println("FAILURE CLEAN EXIT OK");
            else Serial.println("FAILURE CLEAN EXIT FAILED; both drivers released");
            return;
          }
        }
        if (!rollDone) {
          Serial.print("FILM DOWNLOAD STOPPED: ");
          Serial.print(MAX_RECORDS_PER_ROLL);
          Serial.println("-record roll limit reached");
          if (cleanExit()) Serial.println("FAILURE CLEAN EXIT OK");
          else Serial.println("FAILURE CLEAN EXIT FAILED; both drivers released");
          return;
        }

        const size_t used = readE3WithZeroReplyRetry(packet, sizeof(packet));
        if (used == 4 && packet[0] == 0xE3 && packet[1] == 0x01 &&
            packet[2] == 0x00 && packet[3] == 0x00 &&
            packetPayloadChecksumValid(packet, used)) {
          Serial.println("FILM E3 ALL END: E3 01 00 00");
          allDone = true;
        } else if (used == 36 && packet[0] == 0xE3 && packet[1] == 0x21 &&
                   packetPayloadChecksumValid(packet, used)) {
          ++rolls;
          printPacket("FILM E3 HEADER", packet, used, 36);
        } else {
          Serial.println("FILM DOWNLOAD FAILED at next E3");
          printPacket("PARTIAL E3", packet, used, 36);
          if (cleanExit()) Serial.println("FAILURE CLEAN EXIT OK");
          else Serial.println("FAILURE CLEAN EXIT FAILED; both drivers released");
          return;
        }
      }

      if (!allDone) {
        Serial.print("FILM DOWNLOAD STOPPED: ");
        Serial.print(MAX_FILM_ROLLS);
        Serial.println("-roll camera capacity reached without all-end marker");
        if (cleanExit()) Serial.println("FAILURE CLEAN EXIT OK");
        else Serial.println("FAILURE CLEAN EXIT FAILED; both drivers released");
        return;
      }
      Serial.print("FILM DOWNLOAD READ OK: rolls=");
      Serial.print(rolls);
      Serial.print(" records=");
      Serial.println(totalRecords);
    }
  } else {
    Serial.println("READ SETTINGS FLOW OK");
  }
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

failed:
  Serial.print("READ SETTINGS FLOW FAILED at ");
  Serial.println(stage);
  if (strcmp(stage, "film E1") == 0) printPacket("PARTIAL FILM E1", e1c, ne1c, 5);
  if (strcmp(stage, "E3 film header") == 0) {
    printPacket("FILM E1 BEFORE E3", e1c, ne1c, 5);
    printPacket("PARTIAL E3", e3, ne3, 36);
    printSamples("E3", lastSamples);
  }
  if (sessionEstablished) {
    if (cleanExit()) Serial.println("FAILURE CLEAN EXIT OK");
    else Serial.println("FAILURE CLEAN EXIT FAILED; both drivers released");
  } else {
    activeHighMode = false;
    releaseLine();
  }
}

static void runCustomFunctionReadFlow() {
  uint8_t ff1[1], f6a[17], f1a[6], sync[1], f6b[17];
  uint8_t ff2[1], f1b[6], d5[13], d7[13], d9[13], d1[14];
  size_t nff1, nf6a, nf1a, nsync, nf6b, nff2, nf1b, nd5, nd7, nd9, nd1;
  const char *stage = "initial FF";

  drainRx();
  activeHighMode = false;
  releaseLine();
  Serial.println("Running captured custom-function read flow; output resumes when complete");

  sendByte(0xFF);
  nff1 = receiveQuiet(ff1, 1, 500);
  if (nff1 != 1 || ff1[0] != 0xF4) goto failed;
  stage = "initial F6";
  sendByte(0xF4);
  delay(300);
  sendByte(0xF6);
  nf6a = receiveQuiet(f6a, 17, 500);
  if (nf6a != 17 || f6a[0] != 0xF6) goto failed;
  stage = "initial F1";
  nf1a = commandWithF4Retry(0xF1, f1a, 6, 500);
  if (nf1a != 6 || f1a[0] != 0xF1) goto failed;

  stage = "camera F4 sync";
  nsync = receiveQuiet(sync, 1, 1500);
  nf6b = 0;
  if (nsync == 1) {
    if (sync[0] != 0xF4) goto failed;
    stage = "sync F6";
    sendByte(0xF4);
    delay(2);
    sendByte(0xF6);
    nf6b = receiveQuiet(f6b, 17, 500);
    if (nf6b != 17 || f6b[0] != 0xF6) goto failed;
  }

  stage = "custom-function FF";
  delay(1200);
  sendByte(0xFF);
  nff2 = receiveQuiet(ff2, 1, 500);
  if (nff2 != 1 || ff2[0] != 0xF4) goto failed;
  sendByte(0xF4);
  delay(63);
  stage = "custom-function F1";
  nf1b = commandWithF4Retry(0xF1, f1b, 6, 500);
  if (nf1b != 6 || f1b[0] != 0xF1) goto failed;

  stage = "D5";
  delay(78);
  nd5 = commandWithF4Retry(0xD5, d5, 13, 500);
  if (nd5 != 13 || d5[0] != 0xD5) goto failed;

  stage = "D7";
  delay(63);
  nd7 = commandWithF4Retry(0xD7, d7, 13, 500);
  if (nd7 != 13 || d7[0] != 0xD7) goto failed;

  stage = "D9";
  delay(63);
  nd9 = commandWithF4Retry(0xD9, d9, 13, 500);
  if (nd9 != 13 || d9[0] != 0xD9) goto failed;

  stage = "D1";
  delay(78);
  nd1 = commandWithF4Retry(0xD1, d1, 14, 500);
  if (nd1 != 14 || d1[0] != 0xD1) goto failed;

  printPacket("READ D5", d5, nd5, 13);
  printPacket("READ D7", d7, nd7, 13);
  printPacket("READ D9", d9, nd9, 13);
  printPacket("READ D1", d1, nd1, 14);
  Serial.println("CUSTOM-FUNCTION READ FLOW OK");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

failed:
  Serial.print("CUSTOM-FUNCTION READ FLOW FAILED at ");
  Serial.println(stage);
  activeHighMode = false;
  releaseLine();
}

static bool beginCapturedFeatureSession(const char *name) {
  uint8_t reply[17];
  drainRx();
  activeHighMode = false;
  releaseLine();
  Serial.print("Running captured ");
  Serial.print(name);
  Serial.println(" read flow; output resumes when complete");

  sendByte(0xFF);
  if (receiveQuiet(reply, 1, 500) != 1 || reply[0] != 0xF4) return false;
  sendByte(0xF4);
  delay(300);
  sendByte(0xF6);
  if (receiveQuiet(reply, 17, 500) != 17 || reply[0] != 0xF6) return false;
  if (commandWithF4Retry(0xF1, reply, 6, 500) != 6 || reply[0] != 0xF1) return false;

  // The refresh F4 is asynchronous and is not emitted on every run.
  const size_t syncUsed = receiveQuiet(reply, 1, 1500);
  if (syncUsed == 1) {
    if (reply[0] != 0xF4) return false;
    sendByte(0xF4);
    delay(2);
    sendByte(0xF6);
    if (receiveQuiet(reply, 17, 500) != 17 || reply[0] != 0xF6) return false;
  }

  delay(1200);
  sendByte(0xFF);
  if (receiveQuiet(reply, 1, 500) != 1 || reply[0] != 0xF4) return false;
  sendByte(0xF4);
  delay(63);
  return commandWithF4Retry(0xF1, reply, 6, 500) == 6 && reply[0] == 0xF1;
}

static void runEstablishedSessionPowerLossWindow() {
  if (!beginCapturedFeatureSession("established-session power-loss")) {
    activeHighMode = false;
    releaseLine();
    Serial.println("POWER-LOSS TEST FAILED before session establishment; both drivers released");
    return;
  }
  Serial.println("SESSION ESTABLISHED; unplug UNO USB now to test sudden power loss");
  Serial.println("If left connected, the firmware will cleanly exit after 20 seconds");
  const uint32_t deadline = millis() + 20000;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    // Keep the protocol line released while the established session is idle.
    activeHighMode = false;
    releaseLine();
  }
  if (cleanExit()) Serial.println("POWER-LOSS WINDOW EXPIRED; CLEAN EXIT OK");
  else Serial.println("POWER-LOSS WINDOW EXPIRED; CLEAN EXIT FAILED");
}

static void runPersonalFunctionReadFlow() {
  const uint8_t commands[] = {
    0xD3, 0xDD, 0xC5, 0xC6, 0xC1, 0xC3, 0xC4, 0xCB,
    0xCC, 0xCA, 0xC7, 0xC8, 0xC0, 0xCD, 0xCF, 0xCE
  };
  uint8_t packet[16];
  if (!beginCapturedFeatureSession("personal-function")) goto failed;

  for (size_t i = 0; i < sizeof(commands); ++i) {
    size_t used = 0;
    bool valid = false;
    for (uint8_t attempt = 1; attempt <= 3 && !valid; ++attempt) {
      delay(attempt == 1 ? 78 : 150);
      used = variablePacketWithF4Retry(commands[i], packet, sizeof(packet), 1000);
      valid = used >= 4 && packet[0] == commands[i] &&
              used == static_cast<size_t>(packet[1]) + 3;
      if (!valid) {
        Serial.print("P.Fn command ");
        printHex(commands[i]);
        Serial.print(" attempt ");
        Serial.print(attempt);
        Serial.println(" invalid");
        printPacket("P.Fn PARTIAL", packet, used, used >= 2 ? static_cast<size_t>(packet[1]) + 3 : 0);
        // Let a late response finish, then discard it before retrying this read-only command.
        delay(100);
        drainRx();
      }
    }
    if (!valid) {
      Serial.print("P.Fn read failed after retries at command ");
      printHex(commands[i]);
      Serial.println();
      goto failed;
    }
    printPacket("P.Fn BLOCK", packet, used, used);
  }

  Serial.println("PERSONAL-FUNCTION READ FLOW OK");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("PERSONAL-FUNCTION READ FLOW FAILED; both drivers released");
}

static bool writeTwoByteSetting(uint8_t command, uint8_t first, uint8_t second) {
  uint8_t reply[1];
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    sendByte(command);
    if (receiveQuiet(reply, 1, 1000) != 1) continue;
    if (reply[0] == 0xF4) {
      sendByte(0xF4);
      delay(100);
      continue;
    }
    if (reply[0] != command) return false;
    sendByte(2);
    sendByte(first);
    sendByte(second);
    sendByte(static_cast<uint8_t>(first + second));
    return receiveQuiet(reply, 1, 1000) == 1 && reply[0] == 0x01;
  }
  return false;
}

static bool readPfn4(uint8_t *packet, const char *label) {
  delay(78);
  const size_t used = variablePacketWithF4Retry(0xC3, packet, 5, 1000);
  printPacket(label, packet, used, 5);
  return used == 5 && packet[0] == 0xC3 && packet[1] == 2 &&
         packet[4] == static_cast<uint8_t>(packet[2] + packet[3]);
}

static void runPfn4RoundTripTest() {
  uint8_t packet[5];
  bool changed = false;
  if (!beginCapturedFeatureSession("P.Fn-4 round-trip")) goto failed;
  if (!readPfn4(packet, "P.Fn-4 ORIGINAL")) goto failed;
  if (packet[2] != 0xA0 || packet[3] != 0x10) {
    Serial.println("ABORT: expected original A0 10; nothing was written");
    cleanExit();
    return;
  }

  delay(78);
  if (!writeTwoByteSetting(0xB3, 0x98, 0x10)) goto failed;
  changed = true;
  Serial.println("P.Fn-4 TEMP WRITE ACKNOWLEDGED: 1/4000 s .. 30 s");
  if (!readPfn4(packet, "P.Fn-4 TEMP VERIFY") || packet[2] != 0x98 || packet[3] != 0x10) goto restore_failed;

  delay(78);
  if (!writeTwoByteSetting(0xB3, 0xA0, 0x10)) goto restore_failed;
  changed = false;
  if (!readPfn4(packet, "P.Fn-4 RESTORE VERIFY") || packet[2] != 0xA0 || packet[3] != 0x10) goto restore_failed;
  Serial.println("P.Fn-4 ROUND-TRIP OK; original value restored");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

restore_failed:
  // Make several best-effort restore attempts before releasing the bus.
  for (uint8_t attempt = 1; attempt <= 5 && changed; ++attempt) {
    delay(150);
    if (writeTwoByteSetting(0xB3, 0xA0, 0x10)) changed = false;
  }
  if (changed) Serial.println("RESTORE FAILED: P.Fn-4 may still be 1/4000 s .. 30 s");
  else Serial.println("RESTORE ACKNOWLEDGED after retry");
  cleanExit();
  return;

failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("P.Fn-4 ROUND-TRIP FAILED before temporary change");
}

static void runPfn4RestoreOnly() {
  uint8_t packet[5];
  if (!beginCapturedFeatureSession("P.Fn-4 restore-only")) goto failed;
  if (!readPfn4(packet, "P.Fn-4 BEFORE RESTORE")) goto failed;
  if (packet[2] == 0xA0 && packet[3] == 0x10) {
    Serial.println("P.Fn-4 ALREADY RESTORED");
  } else {
    delay(150);
    if (!writeTwoByteSetting(0xB3, 0xA0, 0x10)) goto failed;
    Serial.println("P.Fn-4 RESTORE WRITE ACKNOWLEDGED");
    if (!readPfn4(packet, "P.Fn-4 RESTORE VERIFY") || packet[2] != 0xA0 || packet[3] != 0x10) goto failed;
    Serial.println("P.Fn-4 RESTORE VERIFIED: 1/8000 s .. 30 s");
  }
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("P.Fn-4 RESTORE-ONLY FAILED; both drivers released");
}

static bool readPfn5(uint8_t *packet, const char *label) {
  delay(78);
  const size_t used = variablePacketWithF4Retry(0xC4, packet, 5, 1000);
  printPacket(label, packet, used, 5);
  return used == 5 && packet[0] == 0xC4 && packet[1] == 2 &&
         packet[4] == static_cast<uint8_t>(packet[2] + packet[3]);
}

static void runPfn5TemporaryTest() {
  uint8_t packet[5];
  if (!beginCapturedFeatureSession("P.Fn-5 temporary write")) goto failed;
  if (!readPfn5(packet, "P.Fn-5 ORIGINAL")) goto failed;
  if (packet[2] != 0x70 || packet[3] != 0x08) {
    Serial.println("ABORT: expected original P.Fn-5 value 70 08; nothing was written");
    cleanExit();
    return;
  }
  delay(78);
  if (!writeTwoByteSetting(0xB4, 0x68, 0x08)) goto uncertain;
  Serial.println("P.Fn-5 TEMP WRITE ACKNOWLEDGED: f/64 .. f/1.0");
  if (!readPfn5(packet, "P.Fn-5 TEMP VERIFY") || packet[2] != 0x68 || packet[3] != 0x08) goto uncertain;
  Serial.println("P.Fn-5 TEST OK: minimum aperture is temporarily f/64");
  Serial.println("IMPORTANT: enter a fresh PC mode and send b to restore f/91 .. f/1.0");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
uncertain:
  Serial.println("P.Fn-5 WRITE/VERIFY UNCERTAIN: enter fresh PC mode and send b to restore");
  cleanExit();
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("P.Fn-5 TEST FAILED before a confirmed change; both drivers released");
}

static void runPfn5Restore() {
  uint8_t packet[5];
  if (!beginCapturedFeatureSession("P.Fn-5 restore")) goto failed;
  if (!readPfn5(packet, "P.Fn-5 BEFORE RESTORE")) goto failed;
  if (packet[2] == 0x70 && packet[3] == 0x08) {
    Serial.println("P.Fn-5 ALREADY RESTORED: f/91 .. f/1.0");
  } else if (packet[2] == 0x68 && packet[3] == 0x08) {
    delay(78);
    if (!writeTwoByteSetting(0xB4, 0x70, 0x08)) goto failed;
    Serial.println("P.Fn-5 RESTORE WRITE ACKNOWLEDGED");
    if (!readPfn5(packet, "P.Fn-5 RESTORE VERIFY") || packet[2] != 0x70 || packet[3] != 0x08) goto failed;
    Serial.println("P.Fn-5 RESTORE VERIFIED: f/91 .. f/1.0");
  } else {
    Serial.println("ABORT: P.Fn-5 is neither known test value nor original; nothing was written");
  }
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("P.Fn-5 RESTORE FAILED; retry b from a fresh PC mode");
}

static const uint8_t SHOOTING_MASK_ORIGINAL[8] = {0xFF, 0xFF, 0x0C, 0x3F, 0x00, 0x08, 0x7F, 0x00};
static const uint8_t SHOOTING_MASK_TEST[8] = {0xF7, 0xFF, 0x0C, 0x3F, 0x00, 0x08, 0x7F, 0x00};
static const uint8_t SHOOTING_MASK_16_BYTE[8] = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t SHOOTING_MASK_8_BYTE[8] = {0xF6, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static bool readShootingMask(uint8_t *mask, const char *label) {
  uint8_t packet[11];
  delay(78);
  const size_t used = variablePacketWithF4Retry(0xE8, packet, sizeof(packet), 1000);
  printPacket(label, packet, used, 11);
  if (used != 11 || packet[0] != 0xE8 || packet[1] != 8) return false;
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    mask[i] = packet[i + 2];
    checksum = static_cast<uint8_t>(checksum + mask[i]);
  }
  return checksum == packet[10];
}

static bool maskEquals(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, 8) == 0;
}

static bool writeDataSetting(uint8_t command, const uint8_t *data, uint8_t length) {
  uint8_t reply[1];
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    drainRx();
    sendByte(command);
    if (receiveQuiet(reply, 1, 1000) != 1) continue;
    if (reply[0] == 0xF4) {
      sendByte(0xF4);
      delay(100);
      continue;
    }
    if (reply[0] != command) return false;
    uint8_t checksum = 0;
    sendByte(length);
    for (uint8_t i = 0; i < length; ++i) {
      sendByte(data[i]);
      checksum = static_cast<uint8_t>(checksum + data[i]);
    }
    sendByte(checksum);
    return receiveQuiet(reply, 1, 1000) == 1 && reply[0] == 0x01;
  }
  return false;
}

static bool validateShootingMask(const uint8_t *mask, uint8_t requestedWidth,
                                 uint8_t *calculatedWidth) {
  const uint8_t allowed[8] = {0xFF, 0xFF, 0x0C, 0x3F, 0x7F, 0xF8, 0x7F, 0x3F};
  for (uint8_t i = 0; i < 8; ++i) {
    if ((mask[i] & static_cast<uint8_t>(~allowed[i])) != 0) {
      Serial.print("SAFETY REJECT: reserved shooting-mask bits in byte ");
      Serial.println(i);
      return false;
    }
  }
  if ((mask[0] & 0xC0) != 0xC0 || (mask[1] & 0x09) != 0x09) {
    Serial.println("SAFETY REJECT: mandatory base bits C0/09 are missing");
    return false;
  }
  if ((mask[0] & 0x30) != 0 && (mask[0] & 0x30) != 0x30) {
    Serial.println("SAFETY REJECT: focal-length composite mask is incomplete");
    return false;
  }
  if (mask[2] != 0 && mask[2] != 0x0C) {
    Serial.println("SAFETY REJECT: bulb-time composite mask is incomplete");
    return false;
  }
  if ((mask[3] & 0x38) != 0 && (mask[3] & 0x38) != 0x38) {
    Serial.println("SAFETY REJECT: date composite mask is incomplete");
    return false;
  }
  if ((mask[3] & 0x07) != 0 && (mask[3] & 0x07) != 0x07) {
    Serial.println("SAFETY REJECT: time composite mask is incomplete");
    return false;
  }
  const bool anyCustom = mask[4] != 0 || (mask[5] & 0xF0) != 0;
  if (anyCustom && (mask[4] != 0x7F || (mask[5] & 0xF0) != 0xF0)) {
    Serial.println("SAFETY REJECT: custom-function composite mask is incomplete");
    return false;
  }
  if (mask[6] != 0 && mask[6] != 0x7F) {
    Serial.println("SAFETY REJECT: focus-points composite mask is incomplete");
    return false;
  }
  if (mask[7] != 0 && mask[7] != 0x3F) {
    Serial.println("SAFETY REJECT: battery-datetime composite mask is incomplete");
    return false;
  }
  if (mask[2] == 0x0C && (mask[0] & 0x04) == 0) {
    Serial.println("SAFETY REJECT: bulb time requires shutter speed");
    return false;
  }
  if (mask[6] == 0x7F && (mask[1] & 0x02) == 0) {
    Serial.println("SAFETY REJECT: focus points require AF mode");
    return false;
  }

  uint8_t selectable = 0;
  if ((mask[0] & 0x30) == 0x30) selectable += 2;
  for (uint8_t bit = 0x08; bit != 0; bit >>= 1) if (mask[0] & bit) ++selectable;
  const uint8_t byte1Fields[] = {0x80, 0x40, 0x20, 0x10, 0x04, 0x02};
  for (uint8_t i = 0; i < sizeof(byte1Fields); ++i) if (mask[1] & byte1Fields[i]) ++selectable;
  if (mask[2] == 0x0C) selectable += 2;
  if ((mask[3] & 0x38) == 0x38) selectable += 3;
  if ((mask[3] & 0x07) == 0x07) selectable += 3;
  if (anyCustom) selectable += 11;
  if (mask[5] & 0x08) ++selectable;
  if (mask[6] == 0x7F) selectable += 7;
  if (mask[7] == 0x3F) selectable += 6;
  if (selectable > 28) {
    Serial.println("SAFETY REJECT: selectable fields exceed 28 bytes");
    return false;
  }
  const uint8_t total = static_cast<uint8_t>(selectable + 4);
  *calculatedWidth = total <= 8 ? 0x08 : (total <= 16 ? 0x10 : 0x20);
  if (requestedWidth != *calculatedWidth) {
    Serial.print("SAFETY REJECT: E7 width mismatch; calculated ");
    Serial.print(*calculatedWidth, HEX);
    Serial.print(", requested ");
    Serial.println(requestedWidth, HEX);
    return false;
  }
  return true;
}

static void runShootingMaskSafetySelfTest() {
  const uint8_t missingBase[8] = {0x36, 0x09, 0, 0, 0, 0, 0, 0};
  const uint8_t partialFocal[8] = {0xD6, 0x09, 0, 0, 0, 0, 0, 0};
  const uint8_t missingDependency[8] = {0xF2, 0x09, 0x0C, 0, 0, 0, 0, 0};
  const uint8_t overLimit[8] = {0xFF, 0xFF, 0x0C, 0x3F, 0x7F, 0xF8, 0x7F, 0x3F};
  uint8_t calculated = 0;
  uint8_t passed = 0;
  if (validateShootingMask(SHOOTING_MASK_ORIGINAL, 0x20, &calculated)) ++passed;
  if (validateShootingMask(SHOOTING_MASK_16_BYTE, 0x10, &calculated)) ++passed;
  if (validateShootingMask(SHOOTING_MASK_8_BYTE, 0x08, &calculated)) ++passed;
  if (!validateShootingMask(missingBase, 0x08, &calculated)) ++passed;
  if (!validateShootingMask(partialFocal, 0x08, &calculated)) ++passed;
  if (!validateShootingMask(missingDependency, 0x08, &calculated)) ++passed;
  if (!validateShootingMask(overLimit, 0x20, &calculated)) ++passed;
  if (!validateShootingMask(SHOOTING_MASK_8_BYTE, 0x20, &calculated)) ++passed;
  Serial.print("SHOOTING MASK SAFETY SELF-TEST: ");
  Serial.print(passed);
  Serial.println("/8 checks passed; no camera command was sent");
}

static bool readCameraId(uint8_t *cameraId, const char *label) {
  uint8_t packet[6];
  delay(78);
  const size_t used = commandWithF4Retry(0xF1, packet, sizeof(packet), 1000);
  printPacket(label, packet, used, 6);
  if (!printCameraIdFromF1(packet, used)) return false;
  *cameraId = static_cast<uint8_t>(packet[3] & 0x7F);
  return true;
}

static void runCameraIdTemporaryTest() {
  uint8_t cameraId = 0;
  const uint8_t testId = 12;
  if (!beginCapturedFeatureSession("camera-ID temporary write")) goto failed;
  if (!readCameraId(&cameraId, "CAMERA ID ORIGINAL")) goto failed;
  if (cameraId != 0) {
    Serial.println("ABORT: expected original camera ID 0; nothing was written");
    cleanExit();
    return;
  }
  delay(78);
  if (!writeDataSetting(0xF9, &testId, 1)) goto uncertain;
  Serial.println("CAMERA ID F9 WRITE ACKNOWLEDGED: 12");
  if (!readCameraId(&cameraId, "CAMERA ID TEMP VERIFY") || cameraId != testId) goto uncertain;
  Serial.println("CAMERA ID TEST OK: ID is temporarily 12");
  Serial.println("IMPORTANT: enter a fresh PC mode and send l to restore ID 0");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
uncertain:
  Serial.println("CAMERA ID WRITE/VERIFY UNCERTAIN: enter fresh PC mode and send l to restore");
  cleanExit();
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("CAMERA ID TEST FAILED before a confirmed change; both drivers released");
}

static void runCameraIdRestore() {
  uint8_t cameraId = 0;
  const uint8_t originalId = 0;
  if (!beginCapturedFeatureSession("camera-ID restore")) goto failed;
  if (!readCameraId(&cameraId, "CAMERA ID BEFORE RESTORE")) goto failed;
  if (cameraId == originalId) {
    Serial.println("CAMERA ID ALREADY RESTORED: 0");
  } else if (cameraId == 12) {
    delay(78);
    if (!writeDataSetting(0xF9, &originalId, 1)) goto failed;
    Serial.println("CAMERA ID F9 RESTORE ACKNOWLEDGED: 0");
    if (!readCameraId(&cameraId, "CAMERA ID RESTORE VERIFY") || cameraId != originalId) goto failed;
    Serial.println("CAMERA ID RESTORE VERIFIED: 0");
  } else {
    Serial.println("ABORT: camera ID is neither test value 12 nor original 0; nothing was written");
  }
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("CAMERA ID RESTORE FAILED; retry l from a fresh PC mode");
}

static bool readFilmStatusPacket(uint8_t command, uint8_t *packet, const char *label) {
  delay(78);
  const size_t used = commandWithF4Retry(command, packet, 5, 1000);
  printPacket(label, packet, used, 5);
  return used == 5 && packet[0] == command && packet[1] == 2 &&
         packetPayloadChecksumValid(packet, used);
}

static bool readE1AfterDelete(uint8_t *packet) {
  const uint16_t waits[] = {200, 500, 1000, 1500};
  for (uint8_t attempt = 0; attempt < 4; ++attempt) {
    delay(waits[attempt]);
    drainRx();
    const size_t used = commandWithF4Retry(0xE1, packet, 5, 1000);
    printPacket("DELETE E1 AFTER", packet, used, 5);
    if (used == 5 && packet[0] == 0xE1 && packet[1] == 2 &&
        packetPayloadChecksumValid(packet, used)) return true;
    if (!(used == 0 || (used == 1 && packet[0] == 0x01))) return false;
    if (attempt < 3) Serial.println("DELETE E1 not ready; waiting before read-only retry");
  }
  return false;
}

static void runFilmDeleteAll() {
  uint8_t e1Before[5], fcBefore[5], e2Reply[2], e1After[5], fcAfter[5];
  uint16_t rollsBefore = 0;
  uint16_t rollsAfter = 0;
  size_t e2Used = 0;
  if (!beginCapturedFeatureSession("film-data delete-all")) goto failed_before_delete;
  if (!readFilmStatusPacket(0xE1, e1Before, "DELETE E1 BEFORE")) goto failed_before_delete;
  if (!readFilmStatusPacket(0xFC, fcBefore, "DELETE FC BEFORE")) goto failed_before_delete;
  rollsBefore = static_cast<uint16_t>(e1Before[2]) << 8 | e1Before[3];
  Serial.print("DELETE PRECHECK: camera reports ");
  Serial.print(rollsBefore);
  Serial.println(" roll segments");
  if (rollsBefore == 0) {
    Serial.println("ABORT: camera already reports zero roll segments; E2 was not sent");
    cleanExit();
    return;
  }

  delay(78);
  e2Used = commandWithF4Retry(0xE2, e2Reply, 2, 1500);
  printPacket("DELETE E2 REPLY", e2Reply, e2Used, 2);
  if (e2Used != 2 || e2Reply[0] != 0xE2 || e2Reply[1] == 0) {
    Serial.println("DELETE RESULT UNCERTAIN: E2 did not return the documented success form");
    cleanExit();
    return;
  }
  Serial.println("DELETE E2 ACKNOWLEDGED; all camera film data may now be erased");

  if (!readE1AfterDelete(e1After)) goto failed_after_delete;
  if (!readFilmStatusPacket(0xFC, fcAfter, "DELETE FC AFTER")) goto failed_after_delete;
  rollsAfter = static_cast<uint16_t>(e1After[2]) << 8 | e1After[3];
  Serial.print("DELETE POSTCHECK: camera reports ");
  Serial.print(rollsAfter);
  Serial.println(" roll segments");
  if (rollsAfter == 0) Serial.println("FILM DATA DELETE VERIFIED IN CURRENT SESSION");
  else Serial.println("FILM DATA DELETE NOT VERIFIED: roll count is still nonzero");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

failed_after_delete:
  Serial.println("DELETE OCCURRED OR IS UNCERTAIN, but post-delete status read failed");
  cleanExit();
  return;
failed_before_delete:
  activeHighMode = false;
  releaseLine();
  Serial.println("FILM DATA DELETE ABORTED before E2; both drivers released");
}

static bool readCheckedPacket(uint8_t command, uint8_t *packet, size_t capacity,
                              size_t expected, const char *label) {
  delay(78);
  const size_t used = variablePacketWithF4Retry(command, packet, capacity, 1000);
  printPacket(label, packet, used, expected);
  return used == expected && packet[0] == command && packetPayloadChecksumValid(packet, used);
}

static void runSingleByteRoundTripStep(uint8_t readCommand, uint8_t writeCommand,
                                       uint8_t expected, uint8_t target,
                                       const char *name, bool restore) {
  uint8_t packet[4];
  if (!beginCapturedFeatureSession(name)) goto failed;
  if (!readCheckedPacket(readCommand, packet, sizeof(packet), 4, "VALUE BEFORE WRITE")) goto failed;
  if (packet[2] == target) {
    Serial.println(restore ? "VALUE ALREADY RESTORED" : "ABORT: test value already active; nothing was written");
  } else if (packet[2] == expected) {
    delay(78);
    if (!writeDataSetting(writeCommand, &target, 1)) goto uncertain;
    Serial.println("WRITE ACKNOWLEDGED");
    if (!readCheckedPacket(readCommand, packet, sizeof(packet), 4, "VALUE VERIFY") || packet[2] != target) goto uncertain;
    Serial.println(restore ? "RESTORE VERIFIED" : "TEMPORARY VALUE VERIFIED");
  } else {
    Serial.println("ABORT: current value is neither expected source nor target; nothing was written");
  }
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
uncertain:
  Serial.println("WRITE/VERIFY UNCERTAIN: use the matching restore command from a fresh PC mode");
  cleanExit();
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("SINGLE-BYTE TEST FAILED before a confirmed change; both drivers released");
}

static void runPfn3Temporary() { runSingleByteRoundTripStep(0xC1, 0xB1, 0x20, 0x10, "P.Fn-3 temporary write", false); }
static void runPfn3Restore() { runSingleByteRoundTripStep(0xC1, 0xB1, 0x10, 0x20, "P.Fn-3 restore", true); }
static void runPfn12Temporary() { runSingleByteRoundTripStep(0xCB, 0xBB, 0x40, 0x00, "P.Fn-12 temporary write", false); }
static void runPfn12Restore() { runSingleByteRoundTripStep(0xCB, 0xBB, 0x00, 0x40, "P.Fn-12 restore", true); }

static bool readPfnGlobal(uint8_t *packet, const char *label) {
  return readCheckedPacket(0xDD, packet, 8, 8, label) && packet[1] == 5;
}

static void runPfnGlobalStep(bool restore) {
  const uint8_t original[5] = {0x13, 0x02, 0xD8, 0x00, 0x00};
  const uint8_t test[5] = {0x13, 0x02, 0xD8, 0x00, 0x01};
  const uint8_t *source = restore ? test : original;
  const uint8_t *target = restore ? original : test;
  uint8_t packet[8];
  if (!beginCapturedFeatureSession(restore ? "P.Fn global restore" : "P.Fn global temporary write")) goto failed;
  if (!readPfnGlobal(packet, "P.Fn GLOBAL BEFORE WRITE")) goto failed;
  if (memcmp(packet + 2, target, 5) == 0) {
    Serial.println(restore ? "P.Fn GLOBAL ALREADY RESTORED" : "ABORT: global test value already active");
  } else if (memcmp(packet + 2, source, 5) == 0) {
    delay(78);
    if (!writeDataSetting(0xDE, target, 5)) goto uncertain;
    Serial.println("P.Fn GLOBAL WRITE ACKNOWLEDGED");
    if (!readPfnGlobal(packet, "P.Fn GLOBAL VERIFY") || memcmp(packet + 2, target, 5) != 0) goto uncertain;
    Serial.println(restore ? "P.Fn GLOBAL RESTORE VERIFIED: trailing byte 00" : "P.Fn GLOBAL TEST VERIFIED: trailing byte 01");
  } else {
    Serial.println("ABORT: P.Fn global block differs from both protected values; nothing was written");
  }
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
uncertain:
  Serial.println("P.Fn GLOBAL WRITE/VERIFY UNCERTAIN: restore from a fresh PC mode");
  cleanExit();
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("P.Fn GLOBAL TEST FAILED before a confirmed change; both drivers released");
}

static bool readCurrentCfn(uint8_t *packet, const char *label) {
  return readCheckedPacket(0xD1, packet, 14, 14, label) && packet[1] == 11;
}

static void runCfn19Step(bool restore) {
  const uint8_t original[11] = {0x21, 0x81, 0x12, 0x21, 0x11, 0x11, 0x11, 0x11, 0x11, 0x01, 0x02};
  uint8_t test[11];
  memcpy(test, original, sizeof(test));
  test[9] = 0x08;
  const uint8_t *source = restore ? test : original;
  const uint8_t *target = restore ? original : test;
  uint8_t packet[14];
  if (!beginCapturedFeatureSession(restore ? "C.Fn-19 restore" : "C.Fn-19 option-3 write")) goto failed;
  if (!readCurrentCfn(packet, "C.Fn CURRENT BEFORE WRITE")) goto failed;
  if (memcmp(packet + 2, target, 11) == 0) {
    Serial.println(restore ? "C.Fn-19 ALREADY RESTORED" : "ABORT: C.Fn-19 option 3 already active");
  } else if (memcmp(packet + 2, source, 11) == 0) {
    delay(78);
    if (!writeDataSetting(0xD2, target, 11)) goto uncertain;
    Serial.println("C.Fn D2 WRITE ACKNOWLEDGED");
    if (!readCurrentCfn(packet, "C.Fn CURRENT VERIFY") || memcmp(packet + 2, target, 11) != 0) goto uncertain;
    Serial.println(restore ? "C.Fn-19 RESTORE VERIFIED: option 0 / wire 01" : "C.Fn-19 TEST VERIFIED: option 3 / wire 08");
  } else {
    Serial.println("ABORT: current C.Fn block differs from protected baseline; nothing was written");
  }
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
uncertain:
  Serial.println("C.Fn-19 WRITE/VERIFY UNCERTAIN: restore from a fresh PC mode");
  cleanExit();
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("C.Fn-19 TEST FAILED before a confirmed change; both drivers released");
}

static bool writeShootingMask(const uint8_t *mask, uint8_t recordWidth) {
  uint8_t calculatedWidth = 0;
  if (!validateShootingMask(mask, recordWidth, &calculatedWidth)) return false;
  Serial.print("SHOOTING MASK SAFETY CHECK OK: E7=");
  Serial.println(calculatedWidth, HEX);
  delay(78);
  if (!writeDataSetting(0xE7, &recordWidth, 1)) return false;
  Serial.print("SHOOTING FIELD E7 WIDTH ACKNOWLEDGED: ");
  Serial.print(recordWidth, HEX);
  Serial.println(" hex bytes");
  delay(78);
  if (!writeDataSetting(0xE9, mask, 8)) return false;
  Serial.println("SHOOTING FIELD E9 MASK ACKNOWLEDGED");
  return true;
}

static void runShootingFieldTemporaryTest() {
  uint8_t mask[8];
  if (!beginCapturedFeatureSession("shooting-field temporary write")) goto failed;
  if (!readShootingMask(mask, "SHOOTING MASK ORIGINAL")) goto failed;
  if (!maskEquals(mask, SHOOTING_MASK_ORIGINAL)) {
    Serial.println("ABORT: current mask is not the expected original; nothing was written");
    cleanExit();
    return;
  }
  if (!writeShootingMask(SHOOTING_MASK_TEST, 0x20)) goto uncertain;
  if (!readShootingMask(mask, "SHOOTING MASK TEMP VERIFY") ||
      !maskEquals(mask, SHOOTING_MASK_TEST)) goto uncertain;
  Serial.println("SHOOTING FIELD TEST OK: max-aperture recording is temporarily disabled");
  Serial.println("IMPORTANT: enter a fresh PC mode and send x to restore the original mask");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

uncertain:
  Serial.println("WRITE/VERIFY UNCERTAIN: enter a fresh PC mode and send x to restore safely");
  cleanExit();
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("SHOOTING FIELD TEST FAILED before a confirmed change; both drivers released");
}

static void runShootingFieldRestore() {
  uint8_t mask[8];
  if (!beginCapturedFeatureSession("shooting-field restore")) goto failed;
  if (!readShootingMask(mask, "SHOOTING MASK BEFORE RESTORE")) goto failed;
  if (maskEquals(mask, SHOOTING_MASK_ORIGINAL)) {
    Serial.println("SHOOTING FIELD MASK ALREADY RESTORED");
  } else if (maskEquals(mask, SHOOTING_MASK_TEST) ||
             maskEquals(mask, SHOOTING_MASK_16_BYTE) ||
             maskEquals(mask, SHOOTING_MASK_8_BYTE)) {
    if (!writeShootingMask(SHOOTING_MASK_ORIGINAL, 0x20)) goto failed;
    if (!readShootingMask(mask, "SHOOTING MASK RESTORE VERIFY") ||
        !maskEquals(mask, SHOOTING_MASK_ORIGINAL)) goto failed;
    Serial.println("SHOOTING FIELD MASK RESTORE VERIFIED");
  } else {
    Serial.println("ABORT: mask is neither a known test value nor the original; nothing was written");
  }
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("SHOOTING FIELD RESTORE FAILED; retry x from a fresh PC mode");
}

static void runShootingWidth16Test() {
  uint8_t mask[8];
  if (!beginCapturedFeatureSession("shooting-field 16-byte width write")) goto failed;
  if (!readShootingMask(mask, "SHOOTING MASK ORIGINAL")) goto failed;
  if (!maskEquals(mask, SHOOTING_MASK_ORIGINAL)) {
    Serial.println("ABORT: current mask is not the expected original; nothing was written");
    cleanExit();
    return;
  }
  if (!writeShootingMask(SHOOTING_MASK_16_BYTE, 0x10)) goto uncertain;
  if (!readShootingMask(mask, "SHOOTING MASK 16-BYTE VERIFY") ||
      !maskEquals(mask, SHOOTING_MASK_16_BYTE)) goto uncertain;
  Serial.println("SHOOTING WIDTH TEST OK: total record width is temporarily 16 bytes");
  Serial.println("IMPORTANT: enter a fresh PC mode and send x to restore width 20 and original mask");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
uncertain:
  Serial.println("WRITE/VERIFY UNCERTAIN: enter a fresh PC mode and send x to restore safely");
  cleanExit();
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("SHOOTING WIDTH TEST FAILED before a confirmed change; both drivers released");
}

static void runShootingWidth8Test() {
  uint8_t mask[8];
  if (!beginCapturedFeatureSession("shooting-field 8-byte width write")) goto failed;
  if (!readShootingMask(mask, "SHOOTING MASK ORIGINAL")) goto failed;
  if (!maskEquals(mask, SHOOTING_MASK_ORIGINAL)) {
    Serial.println("ABORT: current mask is not the expected original; nothing was written");
    cleanExit();
    return;
  }
  if (!writeShootingMask(SHOOTING_MASK_8_BYTE, 0x08)) goto uncertain;
  if (!readShootingMask(mask, "SHOOTING MASK 8-BYTE VERIFY") ||
      !maskEquals(mask, SHOOTING_MASK_8_BYTE)) goto uncertain;
  Serial.println("SHOOTING WIDTH TEST OK: total record width is temporarily 8 bytes");
  Serial.println("IMPORTANT: enter a fresh PC mode and send x to restore width 20 and original mask");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;
uncertain:
  Serial.println("WRITE/VERIFY UNCERTAIN: enter a fresh PC mode and send x to restore safely");
  cleanExit();
  return;
failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("SHOOTING WIDTH-8 TEST FAILED before a confirmed change; both drivers released");
}

static void runClockReadFlow() {
  uint8_t packet[16];
  if (!beginCapturedFeatureSession("camera-clock")) goto failed;

  for (uint8_t i = 0; i < 2; ++i) {
    delay(63);
    const size_t used = variablePacketWithF4Retry(0xF3, packet, sizeof(packet), 700);
    if (used != 9 || packet[0] != 0xF3 || packet[1] != 6) goto failed;
    printPacket("CLOCK F3", packet, used, 9);
  }

  // These two auxiliary reads were made by Canon Remote in the same dialog.
  delay(63);
  {
    const size_t used = variablePacketWithF4Retry(0xA1, packet, sizeof(packet), 700);
    if (used != 5 || packet[0] != 0xA1 || packet[1] != 2) goto failed;
    printPacket("CLOCK AUX A1", packet, used, 5);
  }
  delay(63);
  {
    const size_t used = variablePacketWithF4Retry(0xD1, packet, sizeof(packet), 700);
    if (used != 14 || packet[0] != 0xD1 || packet[1] != 11) goto failed;
    printPacket("CLOCK AUX D1", packet, used, 14);
  }

  Serial.println("CAMERA-CLOCK READ FLOW OK (F3 data is YY MM DD hh mm ss in BCD)");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("CAMERA-CLOCK READ FLOW FAILED; both drivers released");
}

static bool beginNextLogicalAction(const char *name) {
  uint8_t packet[6];
  Serial.print("CONTINUOUS ACTION: ");
  Serial.println(name);
  // Canon Remote leaves several seconds between high-level dialogs while the
  // physical PC-mode session remains open.  Starting the next FF immediately
  // can overlap a late F1 byte or asynchronous F4 from the previous action.
  const uint32_t settleDeadline = millis() + 3600;
  while (static_cast<int32_t>(millis() - settleDeadline) < 0) {
    if (!Serial1.available()) continue;
    const uint8_t idleByte = static_cast<uint8_t>(Serial1.read());
    if (idleByte == 0xF4) {
      sendByte(0xF4);
      Serial.println("CONT IDLE F4 ACKNOWLEDGED");
    } else {
      Serial.print("CONT IDLE UNEXPECTED BYTE DISCARDED: ");
      printHex(idleByte);
      Serial.println();
    }
  }
  sendByte(0xFF);
  const size_t nff = receiveQuiet(packet, 1, 1000);
  if (nff != 1 || packet[0] != 0xF4) {
    printPacket("CONT FF", packet, nff, 1);
    return false;
  }
  sendByte(0xF4);
  delay(78);
  const size_t nf1 = commandWithF4Retry(0xF1, packet, sizeof(packet), 1000);
  if (nf1 != sizeof(packet) || packet[0] != 0xF1) {
    printPacket("CONT F1", packet, nf1, sizeof(packet));
    return false;
  }
  return true;
}

static void runContinuousReadSequence() {
  static const uint8_t cfnCommands[] = { 0xD5, 0xD7, 0xD9, 0xD1 };
  static const uint8_t cfnLengths[] = { 13, 13, 13, 14 };
  static const uint8_t pfnCommands[] = {
    0xD3, 0xDD, 0xC5, 0xC6, 0xC1, 0xC3, 0xC4, 0xCB,
    0xCC, 0xCA, 0xC7, 0xC8, 0xC0, 0xCD, 0xCF, 0xCE
  };
  uint8_t packet[36];

  if (!beginCapturedFeatureSession("continuous multi-action")) goto failed;

  Serial.println("CONTINUOUS ACTION: C.Fn read");
  for (size_t i = 0; i < sizeof(cfnCommands); ++i) {
    if (!readCheckedPacket(cfnCommands[i], packet, sizeof(packet), cfnLengths[i], "CONT C.Fn")) goto failed;
  }

  if (!beginNextLogicalAction("P.Fn read")) goto failed;
  for (size_t i = 0; i < sizeof(pfnCommands); ++i) {
    delay(78);
    const size_t used = variablePacketWithF4Retry(pfnCommands[i], packet, sizeof(packet), 1000);
    if (used < 4 || packet[0] != pfnCommands[i] ||
        !packetPayloadChecksumValid(packet, used)) goto failed;
    printPacket("CONT P.Fn", packet, used, used);
  }

  if (!beginNextLogicalAction("camera clock")) goto failed;
  if (!readCheckedPacket(0xF3, packet, sizeof(packet), 9, "CONT CLOCK F3")) goto failed;
  if (!readCheckedPacket(0xA1, packet, sizeof(packet), 5, "CONT CLOCK A1")) goto failed;
  if (!readCheckedPacket(0xD1, packet, sizeof(packet), 14, "CONT CLOCK D1")) goto failed;

  if (!beginNextLogicalAction("shooting-data status")) goto failed;
  if (!readCheckedPacket(0xE8, packet, sizeof(packet), 11, "CONT E8")) goto failed;
  if (!readCheckedPacket(0xFC, packet, sizeof(packet), 5, "CONT FC")) goto failed;
  if (!readCheckedPacket(0xE1, packet, sizeof(packet), 5, "CONT E1")) goto failed;

  Serial.println("CONTINUOUS MULTI-ACTION READ OK; sending the only F2 now");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

failed:
  Serial.println("CONTINUOUS MULTI-ACTION READ FAILED; attempting the only cleanup F2");
  // Let any final byte of a partial response arrive before cleanExit drains it.
  delay(150);
  if (cleanExit()) Serial.println("FAILURE CLEAN EXIT OK");
  else Serial.println("FAILURE CLEAN EXIT FAILED; both drivers released");
}

static bool leapYear2000(uint8_t year) {
  const uint16_t fullYear = 2000U + year;
  return (fullYear % 4U == 0U) && ((fullYear % 100U != 0U) || (fullYear % 400U == 0U));
}

static bool parseClockDigits(char digits[12], uint8_t bcd[6]) {
  for (uint8_t i = 0; i < 12; ++i) {
    if (digits[i] < '0' || digits[i] > '9') return false;
  }
  uint8_t values[6];
  for (uint8_t i = 0; i < 6; ++i) {
    values[i] = static_cast<uint8_t>((digits[i * 2] - '0') * 10 + digits[i * 2 + 1] - '0');
    bcd[i] = static_cast<uint8_t>(((digits[i * 2] - '0') << 4) | (digits[i * 2 + 1] - '0'));
  }
  static const uint8_t daysByMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (values[1] < 1 || values[1] > 12 || values[3] > 23 || values[4] > 59 || values[5] > 59) return false;
  uint8_t maxDay = daysByMonth[values[1] - 1];
  if (values[1] == 2 && leapYear2000(values[0])) maxDay = 29;
  return values[2] >= 1 && values[2] <= maxDay;
}

static bool writeClockPacket(const uint8_t bcd[6]) {
  uint8_t response[1];
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    sendByte(0xF8);
    if (receiveQuiet(response, 1, 1000) != 1) continue;
    if (response[0] == 0xF4) {
      sendByte(0xF4);
      delay(100);
      continue;
    }
    if (response[0] != 0xF8) return false;

    uint8_t checksum = 0;
    sendByte(6);
    for (uint8_t i = 0; i < 6; ++i) {
      sendByte(bcd[i]);
      checksum = static_cast<uint8_t>(checksum + bcd[i]);
    }
    sendByte(checksum);
    return receiveQuiet(response, 1, 1000) == 1 && response[0] == 0x01;
  }
  return false;
}

static void runClockWriteFlow(const uint8_t bcd[6]) {
  uint8_t packet[16];
  if (!beginCapturedFeatureSession("camera-clock write")) goto failed;

  // Canon Remote reads these state blocks immediately before F8.
  delay(63);
  {
    const size_t used = variablePacketWithF4Retry(0xA1, packet, sizeof(packet), 1000);
    if (used != 5 || packet[0] != 0xA1 || packet[1] != 2) goto failed;
    printPacket("CLOCK AUX A1", packet, used, 5);
  }
  delay(63);
  {
    const size_t used = variablePacketWithF4Retry(0xD1, packet, sizeof(packet), 1000);
    if (used != 14 || packet[0] != 0xD1 || packet[1] != 11) goto failed;
    printPacket("CLOCK AUX D1", packet, used, 14);
  }

  delay(63);
  if (!writeClockPacket(bcd)) goto failed;
  Serial.println("CLOCK F8 WRITE ACKNOWLEDGED");

  // Read back immediately. The returned second may advance while the transaction runs.
  delay(78);
  {
    const size_t used = variablePacketWithF4Retry(0xF3, packet, sizeof(packet), 1000);
    if (used != 9 || packet[0] != 0xF3 || packet[1] != 6) goto failed;
    printPacket("CLOCK F3 VERIFY", packet, used, 9);
  }

  Serial.println("CAMERA-CLOCK WRITE FLOW OK");
  if (cleanExit()) Serial.println("EXIT OK; camera should now be out of PC mode");
  else Serial.println("EXIT FAILED; both drivers released");
  return;

failed:
  activeHighMode = false;
  releaseLine();
  Serial.println("CAMERA-CLOCK WRITE FLOW FAILED; both drivers released");
}

static void readAndRunClockWrite() {
  char digits[12];
  uint8_t used = 0;
  const uint32_t deadline = millis() + 5000;
  while (used < sizeof(digits) && static_cast<int32_t>(millis() - deadline) < 0) {
    if (!Serial.available()) continue;
    const char c = Serial.read();
    if (c >= '0' && c <= '9') digits[used++] = c;
    else if (c != '\r' && c != '\n' && c != ' ' && c != '\t') {
      Serial.println("CLOCK INPUT ERROR: use sYYMMDDhhmmss");
      return;
    }
  }
  uint8_t bcd[6];
  if (used != sizeof(digits) || !parseClockDigits(digits, bcd)) {
    Serial.println("CLOCK INPUT ERROR: use a valid sYYMMDDhhmmss value");
    return;
  }
  Serial.print("CLOCK TARGET:");
  for (uint8_t i = 0; i < 6; ++i) { Serial.print(' '); printHex(bcd[i]); }
  Serial.println();
  runClockWriteFlow(bcd);
}

static void runHighAssistDiagnostic() {
  Serial.print("Initial DATA-A: ");
  Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW - stop");
  if (!digitalRead(SENSE_PIN)) return;

  pullLow();
  delayMicroseconds(312);
  digitalWrite(LOW_DRIVE_PIN, LOW);
  digitalWrite(HIGH_KICK_PIN, HIGH);
  delayMicroseconds(20);
  Serial.print("DATA-A while D5 HIGH through diode: ");
  Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW - check diode path");

  digitalWrite(HIGH_KICK_PIN, LOW);
  delayMicroseconds(100);
  Serial.print("DATA-A 100 us after assist released: ");
  Serial.println(digitalRead(SENSE_PIN) ? "HIGH" : "LOW");
  releaseLine();
}

void setup() {
  digitalWrite(LOW_DRIVE_PIN, LOW);
  digitalWrite(HIGH_KICK_PIN, LOW);
  pinMode(LOW_DRIVE_PIN, OUTPUT);
  pinMode(HIGH_KICK_PIN, OUTPUT);
  pinMode(SENSE_PIN, INPUT);
  beginCycleCounter();
  Serial1.begin(9600, SERIAL_8N1);
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("EOS-1V isolated dual-driver test ready");
  Serial.println("Confirm fresh PC mode, then send h for the full handshake");
  Serial.println("Send i instead to test the observed immediate F4-to-F6 variant");
  Serial.println("Send r to run the captured read-only settings flow");
  Serial.println("Send d to read settings plus the first film-roll header only");
  Serial.print("Send f to read all film records (max ");
  Serial.print(MAX_FILM_ROLLS);
  Serial.print(" rolls, ");
  Serial.print(MAX_RECORDS_PER_ROLL);
  Serial.println(" records each)");
  Serial.println("Send c to run the captured D5/D7/D9/D1 read flow");
  Serial.println("Send p to read all 16 P.Fn blocks (read-only)");
  Serial.println("Send v to test P.Fn-4 1/8000 -> 1/4000 -> 1/8000 with read-back");
  Serial.println("Send u to force P.Fn-4 back to 1/8000 .. 30 s and verify");
  Serial.println("Send a to change P.Fn-5 f/91 -> f/64 temporarily and verify");
  Serial.println("Send b from a fresh PC mode to restore P.Fn-5 f/91 .. f/1.0");
  Serial.println("Send 3 to test P.Fn-3 20 -> 10; send # from a fresh PC mode to restore 20");
  Serial.println("Send 2 to test P.Fn-12 40 -> 00; send @ from a fresh PC mode to restore 40");
  Serial.println("Send [ to test P.Fn global trailing byte 00 -> 01; send ] to restore 00");
  Serial.println("Send 9 to test C.Fn-19 option 3 wire 08; send ( to restore option 0 wire 01");
  Serial.println("Send w to disable max-aperture shooting-data recording temporarily and verify");
  Serial.println("Send n to switch temporarily from 32-byte to 16-byte shooting records and verify");
  Serial.println("Send o to switch temporarily from 32-byte to 8-byte shooting records and verify");
  Serial.println("Send g to run the shooting-mask safety self-test (offline; sends no camera command)");
  Serial.println("Send j to change camera ID 0 -> 12 temporarily and verify");
  Serial.println("Send l from a fresh PC mode to restore camera ID 12 -> 0");
  Serial.println("DESTRUCTIVE: send uppercase Z, then ! within 10 seconds, to delete all camera film data");
  Serial.println("Send x from a fresh PC mode to restore the original shooting-data mask");
  Serial.println("Send k to read the camera clock and dialog auxiliary data (read-only)");
  Serial.println("Send m for a 20-second established-session USB power-loss test window");
  Serial.println("Send e to read C.Fn, P.Fn, clock and status continuously, then exit once");
  Serial.println("Send sYYMMDDhhmmss to set the camera clock, verify it with F3, and exit");
  Serial.println("q is only the shorter already-established-session diagnostic");
  Serial.println("With DATA-A connected through 5.1k to D2, t tests the diode HIGH path");
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (filmDeleteArmed && static_cast<int32_t>(millis() - filmDeleteDeadline) >= 0) {
      filmDeleteArmed = false;
      Serial.println("FILM DELETE DISARMED: confirmation timeout");
    }
    if (c == 'Z') {
      filmDeleteArmed = true;
      filmDeleteDeadline = millis() + 10000;
      Serial.println("FILM DELETE ARMED FOR 10 SECONDS: send ! to erase all camera film data");
      continue;
    }
    if (c == '!') {
      if (filmDeleteArmed) {
        filmDeleteArmed = false;
        runFilmDeleteAll();
      } else {
        Serial.println("FILM DELETE NOT ARMED; no camera command was sent");
      }
      continue;
    }
    if (c == 'q' || c == 'Q') runTest();
    if (c == 'h' || c == 'H') runFullHandshake(300);
    if (c == 'i' || c == 'I') runFullHandshake(0);
    if (c == 'r' || c == 'R') runReadSettingsFlow(0);
    if (c == 'd' || c == 'D') runReadSettingsFlow(1);
    if (c == 'f' || c == 'F') runReadSettingsFlow(2);
    if (c == 'c' || c == 'C') runCustomFunctionReadFlow();
    if (c == 'p' || c == 'P') runPersonalFunctionReadFlow();
    if (c == 'v' || c == 'V') runPfn4RoundTripTest();
    if (c == 'u' || c == 'U') runPfn4RestoreOnly();
    if (c == 'a' || c == 'A') runPfn5TemporaryTest();
    if (c == 'b' || c == 'B') runPfn5Restore();
    if (c == '3') runPfn3Temporary();
    if (c == '#') runPfn3Restore();
    if (c == '2') runPfn12Temporary();
    if (c == '@') runPfn12Restore();
    if (c == '[') runPfnGlobalStep(false);
    if (c == ']') runPfnGlobalStep(true);
    if (c == '9') runCfn19Step(false);
    if (c == '(') runCfn19Step(true);
    if (c == 'w' || c == 'W') runShootingFieldTemporaryTest();
    if (c == 'n' || c == 'N') runShootingWidth16Test();
    if (c == 'o' || c == 'O') runShootingWidth8Test();
    if (c == 'g' || c == 'G') runShootingMaskSafetySelfTest();
    if (c == 'j' || c == 'J') runCameraIdTemporaryTest();
    if (c == 'l' || c == 'L') runCameraIdRestore();
    if (c == 'x' || c == 'X') runShootingFieldRestore();
    if (c == 'k' || c == 'K') runClockReadFlow();
    if (c == 'm' || c == 'M') runEstablishedSessionPowerLossWindow();
    if (c == 'e' || c == 'E') runContinuousReadSequence();
    if (c == 's' || c == 'S') readAndRunClockWrite();
    if (c == 't' || c == 'T') runHighAssistDiagnostic();
  }
}

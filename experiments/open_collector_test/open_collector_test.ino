// EOS-1V N3 DATA-A open-collector output test for Arduino UNO R4 WiFi.
// External circuit:
//   D4 -- 10k -- NPN base
//   base -- 100k -- emitter
//   emitter -- EOS COMMON and Arduino GND
//   collector -- 1k -- EOS FOCUS / DATA-A
//
// The transistor is OFF when D4 is LOW. Nothing is transmitted automatically.

constexpr uint8_t DRIVE_PIN = 4;
constexpr unsigned long PULL_LOW_MS = 3000;

bool pullingLow = false;
unsigned long releaseAt = 0;

void releaseLine() {
  digitalWrite(DRIVE_PIN, LOW);  // NPN off: camera pull-up produces logic high
  pullingLow = false;
  Serial.println("RELEASED");
}

void pullLineLow() {
  digitalWrite(DRIVE_PIN, HIGH); // NPN on: open-collector logic low
  pullingLow = true;
  releaseAt = millis() + PULL_LOW_MS;
  Serial.println("LOW for 3 seconds; measure DATA-A and collector now");
}

void setup() {
  // Set the inactive level before enabling output to avoid an unwanted pulse.
  digitalWrite(DRIVE_PIN, LOW);
  pinMode(DRIVE_PIN, OUTPUT);

  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("EOS open-collector test ready");
  Serial.println("Send p to pull DATA-A low for 3 seconds; r releases immediately");
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 'p' || c == 'P') {
      pullLineLow();
    } else if (c == 'r' || c == 'R') {
      releaseLine();
    }
  }

  if (pullingLow && static_cast<long>(millis() - releaseAt) >= 0) {
    releaseLine();
  }
}

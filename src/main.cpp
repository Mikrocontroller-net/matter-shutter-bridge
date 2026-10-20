#include <Arduino.h>

#include "cli.h"
#include "matter_app.h"

// Grove yellow wire to the Unit RF433T. HIGH keys the 433 MHz carrier, so the
// pin must be LOW before anything else runs.
static constexpr uint8_t RF_DATA_PIN = 2;

// AtomS3 Lite front button (the screen-side button; active LOW).
static constexpr uint8_t BUTTON_PIN = 41;
static constexpr uint32_t DECOMMISSION_HOLD_MS = 5000;

static uint32_t buttonDownSince = 0;

static void pollButton() {
  if (digitalRead(BUTTON_PIN) == HIGH) {
    buttonDownSince = 0;
    return;
  }
  uint32_t now = millis();
  if (buttonDownSince == 0) {
    buttonDownSince = now;
  } else if (now - buttonDownSince >= DECOMMISSION_HOLD_MS) {
    buttonDownSince = now;  // one decommission per hold; the device restarts anyway
    matter_app::decommission();
  }
}

void setup() {
  pinMode(RF_DATA_PIN, OUTPUT);
  digitalWrite(RF_DATA_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.println("fernomatter: boot");

  matter_app::begin();
}

void loop() {
  matter_app::loop();
  cli::poll();
  pollButton();
  delay(10);
}

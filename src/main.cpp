#include <Arduino.h>

// Grove yellow wire to the Unit RF433T. HIGH keys the 433 MHz carrier, so the
// pin must be LOW before anything else runs.
static constexpr uint8_t RF_DATA_PIN = 2;

void setup() {
  pinMode(RF_DATA_PIN, OUTPUT);
  digitalWrite(RF_DATA_PIN, LOW);

  Serial.begin(115200);
  Serial.println("fernomatter: boot");
}

void loop() {
  Serial.printf("fernomatter: alive, uptime %lu ms\n", millis());
  delay(2000);
}

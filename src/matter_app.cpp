#include "matter_app.h"

#include <Arduino.h>
#include <Matter.h>
#include <Preferences.h>

#include "diag.h"

// Number of covers to expose. Start with 1, then build with -DSPIKE_COVERS=2
// to see how Alexa names several endpoints on one node. The prebuilt
// Arduino Matter libraries allow at most 4 dynamic endpoints.
#ifndef SPIKE_COVERS
#define SPIKE_COVERS 1
#endif

namespace matter_app {

static constexpr uint32_t TRAVEL_MS = 20000;  // simulated full travel, either direction
static constexpr uint32_t TICK_MS = 250;      // position report interval while moving
static constexpr uint16_t CLOSED = 10000;

struct Event {
  uint8_t cover;
  diag::Command command;
  uint16_t target100ths;
};

struct Cover {
  MatterWindowCovering endpoint;
  uint16_t position = 0;  // simulated position, 100ths of a percent
  uint16_t target = 0;
  bool moving = false;
};

static Cover covers[SPIKE_COVERS];
static QueueHandle_t events;
static Preferences prefs;
static uint32_t lastTick = 0;

// Runs on the Matter task: queue the event and return without blocking.
static bool enqueue(uint8_t cover, diag::Command command) {
  Event e{cover, command, covers[cover].endpoint.getTargetLiftPercent100ths()};
  return xQueueSend(events, &e, 0) == pdTRUE;
}

static void savePosition(uint8_t i) {
  char key[8];
  snprintf(key, sizeof(key), "pos%u", i);
  prefs.putUShort(key, covers[i].position);
}

static void report(Cover &c) {
  c.endpoint.setLiftPercentage(c.position / 100);
}

static void setState(Cover &c, MatterWindowCovering::OperationalState_t state) {
  c.endpoint.setOperationalState(MatterWindowCovering::LIFT, state);
  c.endpoint.setOperationalState(MatterWindowCovering::GLOBAL, state);
}

static void stop(uint8_t i) {
  Cover &c = covers[i];
  c.moving = false;
  c.target = c.position;
  report(c);
  setState(c, MatterWindowCovering::STALL);
  savePosition(i);
  Serial.printf("[sim] cover %u stopped at %u\n", i, c.position);
}

static void handle(const Event &e) {
  Cover &c = covers[e.cover];
  diag::record({millis(), c.endpoint.getEndPointId(), e.command, e.target100ths, c.position});

  if (e.command == diag::Command::StopMotion) {
    stop(e.cover);
    return;
  }
  uint16_t target = e.command == diag::Command::UpOrOpen      ? 0
                    : e.command == diag::Command::DownOrClose ? CLOSED
                                                              : e.target100ths;
  if (target == c.position) {
    stop(e.cover);
    return;
  }
  c.target = target;
  c.moving = true;
  setState(c, target > c.position ? MatterWindowCovering::MOVING_DOWN_OR_CLOSE : MatterWindowCovering::MOVING_UP_OR_OPEN);
  Serial.printf("[sim] cover %u moving %u -> %u\n", e.cover, c.position, target);
}

static void advance(uint32_t elapsedMs) {
  uint32_t step = (uint32_t)CLOSED * elapsedMs / TRAVEL_MS;
  for (uint8_t i = 0; i < SPIKE_COVERS; i++) {
    Cover &c = covers[i];
    if (!c.moving) continue;
    if (c.target > c.position) {
      c.position = (c.target - c.position <= step) ? c.target : c.position + step;
    } else {
      c.position = (c.position - c.target <= step) ? c.target : c.position - step;
    }
    report(c);
    if (c.position == c.target) stop(i);
  }
}

void begin() {
  events = xQueueCreate(16, sizeof(Event));
  prefs.begin("spike", false);

  for (uint8_t i = 0; i < SPIKE_COVERS; i++) {
    Cover &c = covers[i];
    char key[8];
    snprintf(key, sizeof(key), "pos%u", i);
    c.position = prefs.getUShort(key, 0);
    c.target = c.position;

    c.endpoint.begin(c.position / 100, 0, MatterWindowCovering::ROLLERSHADE_EXTERIOR);
    c.endpoint.onOpen([i]() { return enqueue(i, diag::Command::UpOrOpen); });
    c.endpoint.onClose([i]() { return enqueue(i, diag::Command::DownOrClose); });
    c.endpoint.onGoToLiftPercentage([i](uint8_t) { return enqueue(i, diag::Command::GoToLift); });
    c.endpoint.onStop([i]() { return enqueue(i, diag::Command::StopMotion); });
  }

  Matter.begin();
  for (uint8_t i = 0; i < SPIKE_COVERS; i++) setState(covers[i], MatterWindowCovering::STALL);
  printStatus();
}

void loop() {
  Event e;
  while (xQueueReceive(events, &e, 0) == pdTRUE) handle(e);

  uint32_t now = millis();
  if (now - lastTick >= TICK_MS) {
    advance(now - lastTick);
    lastTick = now;
  }
}

void printStatus() {
  if (Matter.isDeviceCommissioned()) {
    Serial.println("[matter] commissioned");
  } else {
    Serial.println("[matter] not commissioned; pair with:");
    Serial.printf("[matter] manual code: %s\n", Matter.getManualPairingCode().c_str());
    Serial.printf("[matter] QR code URL: %s\n", Matter.getOnboardingQRCodeUrl().c_str());
  }
  for (uint8_t i = 0; i < SPIKE_COVERS; i++) {
    Cover &c = covers[i];
    Serial.printf("[matter] cover %u: endpoint %u, position %u, target %u, %s\n", i, c.endpoint.getEndPointId(), c.position,
                  c.target, c.moving ? "moving" : "stopped");
  }
}

void decommission() {
  Serial.println("[matter] decommissioning; the device restarts and can be paired again");
  Matter.decommission();
}

}  // namespace matter_app

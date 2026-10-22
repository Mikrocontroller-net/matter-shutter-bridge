#pragma once

#include <stddef.h>
#include <stdint.h>

#include <esp_matter.h>

// One Matter Window Covering per configured shutter. shutter::Controller
// turns Matter targets into UP/DOWN/STOP for rf_tx and estimates position from
// the configured travel times. Positions use the Matter convention,
// 0 = open and 10000 = closed.
namespace covers {

// Creates one endpoint per shutter in settings, in slot order. Must run
// before esp_matter::start() so endpoint IDs come out the same on every boot.
esp_err_t create(esp_matter::node_t *node);

// Starts the worker task that applies commands and reports positions.
void start();

// Console entry points; each queues work for the worker and returns.
void run(size_t index, bool up);  // to an end stop, ignoring polarity flags
void stop(size_t index);
void reconfigure(size_t index);   // re-read name, times and flags from settings

uint16_t endpointOf(size_t index);
void printStatus();

}  // namespace covers

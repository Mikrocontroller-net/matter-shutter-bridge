#pragma once

#include <esp_matter.h>

// One Matter Window Covering per shutter, driven by shutter::Controller with
// simulated travel and no radio: every frame that would go on air is logged.
// Positions use the Matter convention, 0 = open and 10000 = closed.
namespace covers {

// Creates the endpoints. Must run before esp_matter::start() so endpoint IDs
// come out the same on every boot.
esp_err_t create(esp_matter::node_t *node);

// Starts the worker task that applies commands and reports positions.
void start();

void printStatus();

}  // namespace covers

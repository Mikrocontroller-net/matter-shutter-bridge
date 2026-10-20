#pragma once

// Alexa spike (build step 2): Matter Window Covering endpoints driving a
// simulated motor, no RF. Positions use the Matter convention,
// 0 = fully open and 10000 = fully closed.
namespace matter_app {

void begin();
void loop();           // drains the event queue and advances the simulation
void printStatus();
void decommission();

}  // namespace matter_app

#pragma once

#include <esp_err.h>
#include <stdint.h>

#include <fernotron/message.h>

// 433.92 MHz output through the Unit RF433T on GPIO2, timed by the RMT
// peripheral. One task owns the channel, so transmissions never overlap.
namespace rf_tx {

// Takes over GPIO2 with the line held LOW (carrier off). Call after the pin
// has been driven LOW and before anything calls send().
esp_err_t init();

// Queues one command. It gets the next Fernotron counter value and goes out
// `repeats` times with that same counter. Returns false if the command cannot
// be built or the queue is full; refusals for airtime are logged by the task.
bool send(const fernotron::Target &target, fernotron::Command cmd, uint8_t repeats);

}  // namespace rf_tx

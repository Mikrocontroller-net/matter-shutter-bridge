#pragma once

#include <esp_err.h>
#include <stdint.h>

#include <fernotron/message.h>

// 433.92 MHz output through the Unit RF433T on GPIO2, timed by the RMT
// peripheral. One task owns the channel, so transmissions never overlap.
// Commands are collected until 300 ms pass without a new one (at most 2 s) and
// merged per group or for the whole house when every configured motor got the
// same command (fernotron::Batcher); STOPs go first.
namespace rf_tx {

// Takes over GPIO2 with the line held LOW (carrier off). Call after the pin
// has been driven LOW and before anything calls send().
esp_err_t init();

// Queues one command for a central-unit address. Each frame that leaves gets
// the next Fernotron counter value and goes out `repeats` times with that same
// counter. Returns false if the command cannot be built or the queue is full;
// refusals for airtime are logged by the task.
bool send(const fernotron::Target &target, fernotron::Command cmd, uint8_t repeats);

// The motors this device controls: bit m of members[g] for group g, member m.
// Merging into group or broadcast frames assumes this lists every motor paired
// to those groups on the 2411.
void setMembers(const uint8_t members[8]);

}  // namespace rf_tx

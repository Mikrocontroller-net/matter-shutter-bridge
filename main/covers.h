#pragma once

#include <stddef.h>
#include <stdint.h>

#include <esp_matter.h>

/// One Matter Window Covering per configured shutter. shutter::Controller
/// turns Matter targets into UP/DOWN/STOP for rf_tx and estimates position from
/// the configured travel times. Positions use the Matter convention,
/// 0 = open and 10000 = closed.
namespace covers {

/// Creates one bridged endpoint per shutter in settings, in slot order. Must
/// run before esp_matter::start() so endpoint IDs come out the same on every
/// boot.
esp_err_t create(esp_matter::node_t *node);

/// Starts the worker task that applies commands and reports positions.
void start();

/// @name Console entry points
/// Each queues work for the worker task and returns; out-of-range indexes are
/// ignored.
/// @{
/// Runs shutter `index` to an end stop, ignoring the polarity flags.
void run(size_t index, bool up);
/// Sends STOP to shutter `index`.
void stop(size_t index);
/// Re-reads name, times and flags of shutter `index` from settings, and the
/// group membership for merging.
void reconfigure(size_t index);
/// @}

/// Matter endpoint ID of shutter `index`, or 0 if there is none.
uint16_t endpointOf(size_t index);
/// Logs each shutter's estimated position, target and motion.
void printStatus();

}  // namespace covers

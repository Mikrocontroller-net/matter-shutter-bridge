#pragma once

#include <esp_err.h>

/// Remote development over Wi-Fi: firmware updates, console commands and the
/// log, served on port 80 as plain HTTP for the local network.
/// tools/remote.sh is the client.
///
///     POST /ota            body: the app image (build/matter-shutter-bridge.bin)
///     POST /console        body: one console line, e.g. "matter esp status"
///                          (the Matter SDK's own shell commands, such as
///                          "matter config", answer on USB only)
///     GET  /log?since=<n>  log text from byte offset n (all kept text without it)
///
/// Every request needs "Authorization: Bearer <token>" with the device's own
/// token. Authorized replies carry "X-Firmware: <ELF SHA-256 prefix> <app
/// partition>"; /log replies also carry X-Log-Start (offset of the first byte
/// sent; lower than `since` after a restart) and X-Log-Next (`since` for the
/// next poll).
namespace remote {

/// Copies log output, ESP_LOG and the Matter SDK's, into a 16 KB RAM ring from
/// now on; the SDK's per-message traces (chip[EM], except errors) go to USB
/// only. Call early in app_main().
void captureLog();

/// Loads or draws the access token. Call after settings::load() and before
/// esp_matter::start(), while the RNG needs its boot entropy source.
void init();

/// Starts the HTTP server. If this firmware was just installed over the air,
/// also starts its probation: unless Wi-Fi is up and the server running
/// within 60 s, the device returns to the previous firmware. A crash before
/// that returns to it too. Call after esp_matter::start().
esp_err_t start();

/// Tells the probation whether Wi-Fi is up; call from the Matter device event
/// handler.
void setWifiConnected(bool connected);

/// Prints the address, the token and the running firmware with printf, which
/// the log ring does not capture.
void print();

}  // namespace remote

#pragma once

// WiFi access point + HTTP UI: upload firmware, edit config, read the log,
// start / cancel the auto-start countdown.

#include <stdint.h>

namespace web_ui {

// Commands raised by the web UI, consumed by the main loop.
enum class Command : uint8_t { kNone, kStart, kCancel, kRestartCountdown, kReboot };

// Bring up the AP and HTTP server.
void start();

// Tear down HTTP server and WiFi (before BLE DFU: frees the shared radio).
void stop();

bool running();

// Service HTTP clients. Call from loop() while running().
void handle();

// Fetch-and-clear the pending command.
Command take_command();

}  // namespace web_ui

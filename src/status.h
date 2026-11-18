#pragma once

// Shared, read-mostly run status. Written by the main loop / DFU client,
// read by the LED task and the web UI's /status endpoint.

#include <stdint.h>

namespace status {

enum class Phase : uint8_t {
  kIdleNoFw,   // no firmware stored
  kIdleReady,  // firmware stored, waiting for Start (auto-start off/cancelled)
  kCountdown,  // auto-start countdown running
  kScanning,   // looking for the target
  kConnecting, // connected / DFU handshake / cooldowns
  kStreaming,  // sending firmware bytes (see pct)
  kDoneOk,
  kDoneFail,
};

extern volatile Phase    phase;
extern volatile uint8_t  pct;           // 0..100 during kStreaming
extern volatile uint32_t countdown_s;   // seconds left during kCountdown
extern char              last_result[96];

const char* phase_name(Phase p);

}  // namespace status

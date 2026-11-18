#include "status.h"

namespace status {

volatile Phase    phase       = Phase::kIdleNoFw;
volatile uint8_t  pct         = 0;
volatile uint32_t countdown_s = 0;
char              last_result[96] = "";

const char* phase_name(Phase p) {
  switch (p) {
    case Phase::kIdleNoFw:   return "idle_no_fw";
    case Phase::kIdleReady:  return "ready";
    case Phase::kCountdown:  return "countdown";
    case Phase::kScanning:   return "scanning";
    case Phase::kConnecting: return "connecting";
    case Phase::kStreaming:  return "streaming";
    case Phase::kDoneOk:     return "done_ok";
    case Phase::kDoneFail:   return "done_fail";
  }
  return "?";
}

}  // namespace status

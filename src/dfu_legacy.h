#pragma once

#include "ble_scanner.h"
#include "config.h"
#include "storage.h"

namespace dfu_legacy {

enum class Result {
  kOk,
  kButtonlessTriggered,  // peer was in app mode; we kicked it into AdaDFU. Rescan.
  kConnectFailed,
  kServiceMissing,
  kCharMissing,
  kDisconnectedEarly,
  kTimeout,
  kRemoteError,
  kFsError,
};

const char* result_name(Result r);

// Create the NimBLE client. Call once after ble_scanner::begin().
void begin();

// Run the Nordic Legacy DFU (SDK11, as spoken by the Adafruit nRF52
// bootloader) against `target`, streaming the application from `bundle`.
// Blocking. Updates status::phase / status::pct as it goes.
Result run(const ble_scanner::Target& target, storage::Bundle& bundle, const config::Config& cfg);

}  // namespace dfu_legacy

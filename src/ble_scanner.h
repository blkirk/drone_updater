#pragma once

#include <NimBLEDevice.h>

namespace ble_scanner {

// Bootloader advertising name used by the Adafruit nRF52 bootloader in OTA
// DFU mode. Always accepted, regardless of `ble_name`.
constexpr const char* kAdaDfuName = "AdaDFU";

struct Target {
  NimBLEAddress addr;
  int           rssi = 0;
  char          name[32] = {0};
};

// Initialise NimBLE (central only) and apply TX power. Call once at boot.
void begin(int8_t tx_power_dbm);

// Re-apply TX power (config may have changed from the web UI).
void set_tx_power(int8_t tx_power_dbm);

void set_debug(bool on);

// Block until an advertisement matches, or `timeout_ms` passes (0 = forever).
// Accepts an ad (with rssi >= min_rssi) if any of:
//   - its name contains `name_filter` (when non-empty)
//   - its name is "AdaDFU"
//   - `prefer_mac` is set and the ad's MAC equals it or it + 1
//   - `name_filter` is empty and it advertises the Legacy DFU service UUID
bool find_first(Target* out, uint32_t timeout_ms, const char* name_filter, int8_t min_rssi,
                const NimBLEAddress* prefer_mac);

}  // namespace ble_scanner

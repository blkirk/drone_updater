#include "ble_scanner.h"

#include <Arduino.h>

#include <atomic>
#include <string.h>

#include "logger.h"

namespace ble_scanner {

namespace {

const NimBLEUUID kLegacyDfuSvc("00001530-1212-efde-1523-785feabcd123");

std::atomic<bool>    s_found{false};
Target               s_match;
const char*          s_name_filter = nullptr;
int8_t               s_min_rssi    = -127;
const NimBLEAddress* s_prefer_mac  = nullptr;
bool                 s_debug       = false;

// Debug log de-dupe (per MAC + reason) so dense environments don't flood.
struct Seen { uint8_t mac[6]; char reason[8]; };
// Round-robin: phones' rotating random addresses fill the table fast, and a
// fixed table that stops accepting entries re-logs every newcomer forever.
constexpr uint8_t kSeenMax = 64;
Seen    s_seen[kSeenMax];
uint8_t s_seen_count = 0;
uint8_t s_seen_next  = 0;

// MAC bytes are LSB-first (NimBLE / SoftDevice convention). Nordic-derived
// bootloaders entered from an app typically advertise from the app MAC + 1.
bool mac_match_or_plus_one(const uint8_t* addr, const NimBLEAddress* ref) {
  if (!ref) return false;
  const uint8_t* r = ref->getVal();
  if (memcmp(addr, r, 6) == 0) return true;
  uint8_t plus[6];
  memcpy(plus, r, 6);
  for (int i = 0; i < 6; i++) {
    if (++plus[i] != 0) break;
  }
  return memcmp(addr, plus, 6) == 0;
}

void log_seen(const uint8_t* mac, int rssi, const char* name, const char* reason) {
  if (!s_debug) return;
  for (uint8_t i = 0; i < s_seen_count; i++) {
    if (memcmp(s_seen[i].mac, mac, 6) == 0 && strcmp(s_seen[i].reason, reason) == 0) return;
  }
  Seen& e = s_seen[s_seen_next];
  memcpy(e.mac, mac, 6);
  snprintf(e.reason, sizeof(e.reason), "%s", reason);
  s_seen_next = (uint8_t)((s_seen_next + 1) % kSeenMax);
  if (s_seen_count < kSeenMax) s_seen_count++;
  logger::log("scan: %s %02X:%02X:%02X:%02X:%02X:%02X rssi=%d name='%s'", reason, mac[5],
              mac[4], mac[3], mac[2], mac[1], mac[0], rssi, name);
}

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (s_found.load(std::memory_order_acquire)) return;

    char name[32] = {0};
    if (dev->haveName()) snprintf(name, sizeof(name), "%s", dev->getName().c_str());
    const NimBLEAddress& a = dev->getAddress();
    const uint8_t* mac = a.getVal();
    int rssi = dev->getRSSI();

    // Below min_rssi: drop silently, even with scan_debug on. Distant devices
    // are the bulk of a busy environment and would drown the useful lines.
    if (rssi < s_min_rssi) return;

    bool has_filter = s_name_filter && s_name_filter[0];
    bool name_ok    = has_filter && strstr(name, s_name_filter) != nullptr;
    bool ada_ok     = strcmp(name, kAdaDfuName) == 0;
    bool mac_ok     = mac_match_or_plus_one(mac, s_prefer_mac);
    bool uuid_ok    = !has_filter && dev->isAdvertisingService(kLegacyDfuSvc);

    if (!(name_ok || ada_ok || mac_ok || uuid_ok)) {
      log_seen(mac, rssi, name, has_filter ? "name?" : "uuid?");
      return;
    }

    s_match.addr = a;
    s_match.rssi = rssi;
    memcpy(s_match.name, name, sizeof(name));
    s_found.store(true, std::memory_order_release);
  }
};

ScanCb s_cb;

}  // namespace

void set_tx_power(int8_t dbm) {
  if (!NimBLEDevice::setPower(dbm)) {
    logger::log("ble: tx_power %d dBm rejected, keeping default", (int)dbm);
  }
}

void begin(int8_t tx_power_dbm) {
  logger::log("ble: START NimBLE (central only)");
  NimBLEDevice::init("");
  set_tx_power(tx_power_dbm);
  logger::log("ble: STARTED addr=%s tx_power=%d dBm", NimBLEDevice::getAddress().toString().c_str(),
              (int)NimBLEDevice::getPower());

  NimBLEScan* scan = NimBLEDevice::getScan();
  // wantDuplicates=true: an ad first seen while too weak must be re-evaluated
  // as the drone closes in, so we need every report, not just the first.
  scan->setScanCallbacks(&s_cb, true);
  scan->setDuplicateFilter(0);  // controller-level filter off too
  scan->setActiveScan(true);   // names often live in the scan response
  scan->setInterval(100);
  scan->setWindow(100);        // 100% duty: WiFi is off while we scan
  scan->setMaxResults(0);      // don't accumulate results in RAM
}

void set_debug(bool on) { s_debug = on; }

bool find_first(Target* out, uint32_t timeout_ms, const char* name_filter, int8_t min_rssi,
                const NimBLEAddress* prefer_mac) {
  s_found.store(false);
  s_name_filter = name_filter;
  s_min_rssi    = min_rssi;
  s_prefer_mac  = prefer_mac;
  s_seen_count  = 0;
  s_seen_next   = 0;

  NimBLEScan* scan = NimBLEDevice::getScan();
  logger::log("ble: scan START (name='%s' min_rssi=%d timeout=%lu ms%s)",
              name_filter ? name_filter : "", (int)min_rssi, (unsigned long)timeout_ms,
              prefer_mac ? ", +MAC/MAC+1" : "");
  if (!scan->start(0, false, true)) {  // 0 = until stopped
    logger::log("ble: scan START FAILED");
    return false;
  }

  uint32_t t0 = millis();
  while (!s_found.load(std::memory_order_acquire)) {
    if (timeout_ms && millis() - t0 >= timeout_ms) break;
    delay(20);
  }
  scan->stop();
  logger::log("ble: scan STOP (%s after %lu ms)",
              s_found.load() ? "target found" : "timeout", (unsigned long)(millis() - t0));

  if (!s_found.load(std::memory_order_acquire)) return false;
  *out = s_match;
  return true;
}

}  // namespace ble_scanner

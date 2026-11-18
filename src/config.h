#pragma once

// CONFIG: `key=value` text stored at /config.txt on LittleFS and editable from
// the web UI. Pure C++ (no Arduino) so the parser runs in host tests; file I/O
// lives in storage.cpp.

#include <stddef.h>
#include <stdint.h>

namespace config {

struct Config {
  // Substring of the target app's advertised name (e.g. "XIAO_OTA"). Empty =
  // only accept peers advertising the Legacy DFU UUID or named "AdaDFU".
  char     ble_name[32];

  // Packet Receipt Notification cadence (packets per PRN). 0 disables.
  uint16_t prn;

  // Negotiate ATT MTU 247 (244 B per packet write) instead of 23 (20 B).
  bool     high_mtu;

  // DFU attempts before giving up (scan + buttonless don't consume these).
  uint8_t  retries;

  // Seconds to wait after a pre-connect failure.
  uint16_t retry_cooldown;

  // Seconds to wait after a post-connect failure: lets a wedged bootloader's
  // inactivity watchdog reset it before we try again.
  uint16_t wedge_cooldown;

  // Reject advertisements weaker than this (dBm). -127 = no minimum.
  int8_t   min_rssi;

  // BLE TX power in dBm. ESP32-C3/S3 range -24..+20 (rounded by the stack).
  int8_t   tx_power;

  // Per-scan timeout in seconds. 0 = scan forever.
  uint16_t scan_timeout;

  // Log rejected advertisements at or above min_rssi (deduplicated per MAC).
  bool     scan_debug;

  // Seconds between boot (or last web-UI change) and automatic DFU start
  // when a firmware zip is stored. 0 = never auto-start (web UI only).
  uint16_t autostart_delay;

  // WiFi access point. Empty SSID = "DroneDFU-XXXX" from the MAC. Empty or
  // <8-char password = open network.
  char     wifi_ssid[33];
  char     wifi_pass[65];
};

void apply_defaults(Config* c);

// Overlay `key=value` lines from `text` onto `c`. Lines starting with '#' or
// ';' are comments; unknown keys and out-of-range values are ignored.
// Returns the number of keys applied.
int parse(Config* c, const char* text);

// Apply a single key/value pair. Returns true if the key was recognised and
// the value accepted.
bool apply_kv(Config* c, const char* key, const char* val);

// Render `c` back to key=value text (no comments). Returns bytes written
// (excluding NUL), truncated to `len`.
size_t serialize(const Config& c, char* out, size_t len);

// The process-wide working config.
Config& current();

}  // namespace config

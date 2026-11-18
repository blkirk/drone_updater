// drone_updater — ESP32-C3 / ESP32-S3 Super Mini BLE OTA flasher for a Seeed
// XIAO nRF52840 running the Adafruit nRF52 bootloader (Nordic Legacy DFU).
//
// Flow: boot → WiFi AP + web UI → (firmware stored) auto-start countdown →
// WiFi off → scan → [buttonless → rescan for AdaDFU] → DFU → WiFi back on.

#include <Arduino.h>
#include <esp_system.h>

#include "ble_scanner.h"
#include "config.h"
#include "dfu_legacy.h"
#include "led.h"
#include "logger.h"
#include "status.h"
#include "storage.h"
#include "web_ui.h"

namespace {

uint32_t s_countdown_deadline = 0;

void set_result(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void set_result(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(status::last_result, sizeof(status::last_result), fmt, ap);
  va_end(ap);
}

void enter_idle() {
  status::phase       = storage::fw_present() ? status::Phase::kIdleReady : status::Phase::kIdleNoFw;
  status::countdown_s = 0;
}

// (Re)start the auto-start countdown if there's firmware and it's enabled.
void arm_countdown() {
  const config::Config& cfg = config::current();
  if (!storage::fw_present() || cfg.autostart_delay == 0) {
    enter_idle();
    return;
  }
  s_countdown_deadline = millis() + (uint32_t)cfg.autostart_delay * 1000;
  status::countdown_s  = cfg.autostart_delay;
  status::phase        = status::Phase::kCountdown;
  logger::log("auto-start in %u s", cfg.autostart_delay);
}

bool scan_for_target(ble_scanner::Target* out, const NimBLEAddress* prefer_mac) {
  const config::Config& cfg = config::current();
  status::phase = status::Phase::kScanning;
  logger::log("scan: looking for '%s' / %s%s (timeout %s)", cfg.ble_name, ble_scanner::kAdaDfuName,
              prefer_mac ? " / MAC+1" : "",
              cfg.scan_timeout ? String(cfg.scan_timeout).c_str() : "none");
  if (!ble_scanner::find_first(out, (uint32_t)cfg.scan_timeout * 1000, cfg.ble_name, cfg.min_rssi,
                               prefer_mac)) {
    logger::log("scan: timed out, no target found");
    return false;
  }
  logger::log("scan: found %s rssi=%d '%s'", out->addr.toString().c_str(), out->rssi, out->name);
  return true;
}

void cooldown(uint16_t seconds) {
  status::phase = status::Phase::kConnecting;
  delay((uint32_t)seconds * 1000);
}

// Scan + up to `retries` DFU attempts. Scan waits and buttonless triggers do
// not consume retries; only post-scan DFU failures do.
bool run_dfu_sequence() {
  const config::Config& cfg = config::current();
  storage::Bundle bundle;
  char err[96];
  if (!bundle.open(err, sizeof(err))) {
    logger::log("dfu: %s", err);
    set_result("bundle error: %s", err);
    return false;
  }
  logger::log("dfu: bundle '%s' app=%lu B dat=%lu B", storage::fw_name().c_str(),
              (unsigned long)bundle.parsed.bin.size, (unsigned long)bundle.parsed.dat.size);
  logger::log("cfg: ble_name='%s' prn=%u high_mtu=%d retries=%u min_rssi=%d tx_power=%d "
              "scan_timeout=%u retry_cooldown=%u wedge_cooldown=%u",
              cfg.ble_name, cfg.prn, (int)cfg.high_mtu, cfg.retries, (int)cfg.min_rssi,
              (int)cfg.tx_power, cfg.scan_timeout, cfg.retry_cooldown, cfg.wedge_cooldown);

  ble_scanner::set_tx_power(cfg.tx_power);
  ble_scanner::set_debug(cfg.scan_debug);

  // After a buttonless trigger, also accept the app's MAC / MAC+1 so we find
  // the bootloader even if its name doesn't match.
  NimBLEAddress pending_mac;
  bool          have_pending_mac = false;
  uint8_t       buttonless_count = 0;
  uint8_t       attempt          = 0;

  while (attempt < cfg.retries) {
    ble_scanner::Target t;
    if (!scan_for_target(&t, have_pending_mac ? &pending_mac : nullptr)) {
      set_result("no target found (scan timeout)");
      bundle.close();
      return false;
    }

    logger::log("dfu: attempt %u/%u -> %s", attempt + 1, cfg.retries, t.addr.toString().c_str());
    dfu_legacy::Result r = dfu_legacy::run(t, bundle, cfg);
    logger::log("dfu: attempt result: %s", dfu_legacy::result_name(r));

    if (r == dfu_legacy::Result::kButtonlessTriggered) {
      // Guard against a peer that keeps reporting app mode forever.
      if (++buttonless_count > 5) {
        logger::log("dfu: peer never entered bootloader after %u triggers", buttonless_count - 1);
        attempt++;
        buttonless_count = 0;
        have_pending_mac = false;
        continue;
      }
      // Give the XIAO time to reset and bring the bootloader up cleanly.
      logger::log("dfu: buttonless triggered, waiting 3 s for AdaDFU...");
      pending_mac      = t.addr;
      have_pending_mac = true;
      delay(3000);
      continue;
    }
    have_pending_mac = false;

    if (r == dfu_legacy::Result::kOk) {
      logger::log("dfu: SUCCESS, deleting stored firmware");
      set_result("success: %s", storage::fw_name().c_str());
      bundle.close();
      storage::delete_fw();
      return true;
    }

    attempt++;
    logger::log("dfu: attempt %u/%u failed: %s", attempt, cfg.retries, dfu_legacy::result_name(r));
    set_result("attempt %u/%u failed: %s", attempt, cfg.retries, dfu_legacy::result_name(r));
    if (attempt < cfg.retries) {
      // Pre-connect failures: the peer is fine, short wait. Anything after
      // the GATT handshake may have wedged the bootloader's DFU state; wait
      // for its inactivity watchdog.
      bool     wedge  = r != dfu_legacy::Result::kConnectFailed;
      uint16_t wait_s = wedge ? cfg.wedge_cooldown : cfg.retry_cooldown;
      if (wait_s) {
        logger::log("dfu: %s cooldown %u s", wedge ? "wedge" : "retry", wait_s);
        cooldown(wait_s);
      }
    }
  }

  logger::log("dfu: FAILED after %u attempts", cfg.retries);
  set_result("failed after %u attempts", cfg.retries);
  bundle.close();
  return false;
}

void start_dfu(const char* why) {
  logger::log("==== OTA DFU START (trigger: %s) ====", why);
  uint32_t t0 = millis();
  web_ui::stop();  // free the shared 2.4 GHz radio for BLE
  bool ok = run_dfu_sequence();
  status::phase = ok ? status::Phase::kDoneOk : status::Phase::kDoneFail;
  logger::log("==== OTA DFU STOP: %s after %lu s (%s) ====", ok ? "SUCCESS" : "FAILED",
              (unsigned long)((millis() - t0) / 1000), status::last_result);
  web_ui::start();  // so the log can be read after landing
}

const char* reset_reason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int-watchdog";
    case ESP_RST_TASK_WDT:  return "task-watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    default:                return "other";
  }
}

}  // namespace

void setup() {
  // Bigger TX ring so bursts (scan_debug) aren't dropped. The core's HWCDC
  // doesn't block when no USB host is attached (e.g. on drone battery), so
  // the default TX timeout is kept: a 0 ms timeout drops whole lines.
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  led::begin();
  delay(200);

  bool fs_ok = storage::begin();
  logger::begin();
  bool cfg_file = fs_ok && storage::load_config();
  const config::Config& cfg = config::current();

#if defined(BOARD_S3_SUPERMINI)
  const char* board = "ESP32-S3 Super Mini";
#else
  const char* board = "ESP32-C3 Super Mini";
#endif
  logger::log("==== drone_updater boot: %s, built " __DATE__ " " __TIME__ " ====", board);
  logger::log("boot: reset=%s fs=%s cfg=%s heap=%u", reset_reason(), fs_ok ? "ok" : "FAIL",
              cfg_file ? "config.txt" : "defaults", (unsigned)ESP.getFreeHeap());
  logger::log("cfg: ble_name='%s' prn=%u high_mtu=%d retries=%u min_rssi=%d tx_power=%d "
              "scan_timeout=%u autostart_delay=%u", cfg.ble_name, cfg.prn, (int)cfg.high_mtu,
              cfg.retries, (int)cfg.min_rssi, (int)cfg.tx_power, cfg.scan_timeout,
              cfg.autostart_delay);

  ble_scanner::begin(cfg.tx_power);
  dfu_legacy::begin();
  web_ui::start();

  if (storage::fw_present()) {
    firmware_zip::Parsed p;
    char err[96];
    if (!storage::validate_zip(storage::kFwPath, &p, err, sizeof(err))) {
      logger::log("boot: stored firmware invalid (%s), deleting", err);
      storage::delete_fw();
    }
  }
  size_t fw_size = 0;
  if (storage::fw_present(&fw_size)) {
    logger::log("boot: firmware stored: '%s' (%u B)", storage::fw_name().c_str(), (unsigned)fw_size);
  } else {
    logger::log("boot: no firmware stored");
  }
  arm_countdown();
}

void loop() {
  web_ui::handle();

  switch (web_ui::take_command()) {
    case web_ui::Command::kStart:
      if (storage::fw_present()) {
        start_dfu("web");
      } else {
        logger::log("web: start requested but no firmware stored");
      }
      return;
    case web_ui::Command::kCancel:
      if (status::phase == status::Phase::kCountdown) {
        logger::log("web: countdown cancelled with %u s left", (unsigned)status::countdown_s);
        enter_idle();
      }
      break;
    case web_ui::Command::kRestartCountdown:
      arm_countdown();
      break;
    case web_ui::Command::kReboot:
      logger::log("web: reboot requested");
      delay(300);  // let the HTTP response go out
      ESP.restart();
      break;
    case web_ui::Command::kNone:
      break;
  }

  if (status::phase == status::Phase::kCountdown) {
    int32_t left = (int32_t)(s_countdown_deadline - millis());
    if (left <= 0) {
      start_dfu("auto-start");
    } else {
      uint32_t s = (uint32_t)(left + 999) / 1000;
      if (s != status::countdown_s && (s % 30 == 0 || s <= 10)) {
        logger::log("countdown: DFU starts in %u s", (unsigned)s);
      }
      status::countdown_s = s;
    }
  }
  delay(2);
}

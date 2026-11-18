#include "dfu_legacy.h"

#include <Arduino.h>
#include <atomic>

#include "logger.h"
#include "status.h"

namespace dfu_legacy {

// ---------- Nordic Legacy DFU UUIDs ----------
static const NimBLEUUID kSvcUuid ("00001530-1212-efde-1523-785feabcd123");
static const NimBLEUUID kCtrlUuid("00001531-1212-efde-1523-785feabcd123");
static const NimBLEUUID kPktUuid ("00001532-1212-efde-1523-785feabcd123");
static const NimBLEUUID kVerUuid ("00001534-1212-efde-1523-785feabcd123");

// ---------- Opcodes (mirror Nordic's LegacyDfuImpl.java) ----------
static constexpr uint8_t OP_START_DFU             = 0x01;
static constexpr uint8_t OP_INIT_DFU_PARAMS       = 0x02;
static constexpr uint8_t OP_RECEIVE_FW            = 0x03;
static constexpr uint8_t OP_VALIDATE              = 0x04;
static constexpr uint8_t OP_ACTIVATE_AND_RESET    = 0x05;
static constexpr uint8_t OP_RESET                 = 0x06;
static constexpr uint8_t OP_PKT_RECEIPT_NOTIF_REQ = 0x08;
static constexpr uint8_t OP_RESPONSE_CODE         = 0x10;
static constexpr uint8_t OP_PKT_RECEIPT_NOTIF     = 0x11;
static constexpr uint8_t STATUS_SUCCESS           = 0x01;

// ---------- State ----------
struct Notif {
  uint8_t len;
  uint8_t data[20];
};

static NimBLEClient*               s_client = nullptr;
static NimBLERemoteCharacteristic* s_ctrl   = nullptr;
static NimBLERemoteCharacteristic* s_pkt    = nullptr;
static std::atomic<bool>           s_connected{false};
static std::atomic<int>            s_disc_reason{0};
// Control-point notifications are queued (not a single slot) so a PRN and a
// response arriving back-to-back can't overwrite each other.
static QueueHandle_t               s_notifs = nullptr;
// A RECEIVE_FW response that arrived while we were waiting on a PRN.
static bool                        s_have_pending = false;
static Notif                       s_pending;

const char* result_name(Result r) {
  switch (r) {
    case Result::kOk:                  return "ok";
    case Result::kButtonlessTriggered: return "buttonless";
    case Result::kConnectFailed:       return "connect_failed";
    case Result::kServiceMissing:      return "service_missing";
    case Result::kCharMissing:         return "char_missing";
    case Result::kDisconnectedEarly:   return "disconnected";
    case Result::kTimeout:             return "timeout";
    case Result::kRemoteError:         return "remote_error";
    case Result::kFsError:             return "fs_error";
  }
  return "?";
}

// ---------- Callbacks (NimBLE host task: no logging / blocking here) ----------
class ClientCb : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient*) override { s_connected = true; }
  void onDisconnect(NimBLEClient*, int reason) override {
    s_disc_reason = reason;
    s_connected   = false;
  }
};
static ClientCb s_client_cb;

static void on_ctrl_notify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  Notif n;
  n.len = len > sizeof(n.data) ? sizeof(n.data) : (uint8_t)len;
  memcpy(n.data, data, n.len);
  xQueueSend(s_notifs, &n, 0);
}

// ---------- Helpers ----------
static bool s_link_logged_up = false;

// Log a link drop once, from the main task.
static void note_disconnect() {
  if (s_link_logged_up && !s_connected) {
    s_link_logged_up = false;
    logger::log("ble: DISCONNECTED reason=0x%02x", (int)s_disc_reason);
  }
}

static bool wait_disconnected(uint32_t timeout_ms) {
  uint32_t t0 = millis();
  while (s_connected && millis() - t0 < timeout_ms) delay(20);
  note_disconnect();
  return !s_connected;
}

static bool wait_notification(uint32_t timeout_ms, Notif* out) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeout_ms) {
    if (xQueueReceive(s_notifs, out, pdMS_TO_TICKS(20)) == pdTRUE) return true;
    if (!s_connected) return xQueueReceive(s_notifs, out, 0) == pdTRUE;
  }
  return false;
}

// Wait for [0x10, expected_op, status]. Returns status, or 0xFF on error.
static uint8_t consume_response(uint8_t expected_op, uint32_t timeout_ms = 15000) {
  Notif n;
  if (s_have_pending) {
    n = s_pending;
    s_have_pending = false;
  } else if (!wait_notification(timeout_ms, &n)) {
    logger::log("dfu: response timeout (expected op=0x%02x)", expected_op);
    return 0xFF;
  }
  if (n.len < 3 || n.data[0] != OP_RESPONSE_CODE || n.data[1] != expected_op) {
    logger::log("dfu: unexpected response op_class=0x%02x op=0x%02x len=%u", n.data[0],
                n.data[1], n.len);
    return 0xFF;
  }
  return n.data[2];
}

static bool ctrl_write(const uint8_t* data, size_t len) {
  // WRITE_REQ: the bootloader's Control Point doesn't list
  // WriteWithoutResponse, so a WRITE_CMD would be dropped.
  return s_ctrl->writeValue(data, len, true);
}

// Write-without-response on the Packet characteristic, retrying while the
// stack is out of TX buffers. Fails if the link drops or it stays full ~2 s.
static bool pkt_write(const uint8_t* data, size_t len) {
  for (int tries = 0; tries < 200; tries++) {
    if (s_pkt->writeValue(data, len, false)) return true;
    if (!s_connected) return false;
    delay(10);
  }
  return false;
}

static void put_u32le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Error exit: send RESET (0x06) so the bootloader discards its half-finished
// DFU state, and let *it* drop the link. Disconnecting first can leave the
// state in place and the next START_DFU gets INVALID_STATE.
static Result fail(Result r) {
  if (s_connected && s_ctrl) {
    uint8_t reset_cmd[1] = {OP_RESET};
    ctrl_write(reset_cmd, sizeof(reset_cmd));  // result ignored; link may be dying
    if (!wait_disconnected(3000)) {
      logger::log("dfu: peer did not drop link after RESET, forcing");
      s_client->disconnect();
      wait_disconnected(2000);
    }
  }
  return r;
}

void begin() {
  if (!s_notifs) s_notifs = xQueueCreate(8, sizeof(Notif));
  if (!s_client) {
    s_client = NimBLEDevice::createClient();
    s_client->setClientCallbacks(&s_client_cb, false);
    // Conservative link: 15–30 ms interval (vs. the 7.5 ms minimum) gives the
    // bootloader breathing room while it erases/writes flash, and a 6 s
    // supervision timeout rides out brief fades on a moving drone.
    // Units: interval 1.25 ms, timeout 10 ms.
    s_client->setConnectionParams(12, 24, 0, 600);
    s_client->setConnectTimeout(10000);
  }
}

// ---------- DFU run ----------
static Result run_impl(const ble_scanner::Target& target, storage::Bundle& bundle,
                       const config::Config& cfg);

Result run(const ble_scanner::Target& target, storage::Bundle& bundle, const config::Config& cfg) {
  s_link_logged_up = false;
  Result r = run_impl(target, bundle, cfg);
  note_disconnect();
  if (s_connected) {
    logger::log("ble: link still up after DFU step, disconnecting");
    s_client->disconnect();
    wait_disconnected(2000);
  }
  return r;
}

static Result run_impl(const ble_scanner::Target& target, storage::Bundle& bundle,
                       const config::Config& cfg) {
  begin();
  const zip_reader::Entry& bin = bundle.parsed.bin;
  const zip_reader::Entry& dat = bundle.parsed.dat;

  s_connected = false;
  s_ctrl = s_pkt = nullptr;
  s_have_pending = false;
  xQueueReset(s_notifs);
  status::phase = status::Phase::kConnecting;
  status::pct   = 0;

  if (s_client->isConnected()) {
    s_client->disconnect();
    wait_disconnected(2000);
  }

  NimBLEDevice::setMTU(cfg.high_mtu ? 247 : 23);
  logger::log("ble: CONNECT to %s (rssi %d, '%s')...", target.addr.toString().c_str(),
              target.rssi, target.name);
  // exchangeMTU=high_mtu: NimBLE negotiates the MTU as part of connect.
  if (!s_client->connect(target.addr, true, false, cfg.high_mtu)) {
    logger::log("dfu: connect failed (reason %d)", s_client->getLastError());
    return Result::kConnectFailed;
  }
  s_connected      = s_client->isConnected();
  s_link_logged_up = s_connected;
  logger::log("ble: CONNECTED");

  // Connection-stability gate: at low RSSI the link can come up just long
  // enough to report connected, then fail (0x3E). Don't misreport that as a
  // missing service.
  delay(500);
  if (!s_connected) {
    logger::log("dfu: link dropped right after connect (weak signal?) reason=%d",
                (int)s_disc_reason);
    return Result::kConnectFailed;
  }

  uint16_t mtu     = s_client->getMTU();
  uint16_t payload = mtu > 23 ? mtu - 3 : 20;
  if (payload > 244) payload = 244;
  logger::log("dfu: connected, MTU=%u payload=%u interval=%.2f ms", mtu, payload,
              s_client->getConnInfo().getConnInterval() * 1.25f);

  NimBLERemoteService* svc = s_client->getService(kSvcUuid);
  if (!svc) {
    logger::log("dfu: DFU service not present on peer");
    return fail(Result::kServiceMissing);
  }
  s_ctrl = svc->getCharacteristic(kCtrlUuid);
  s_pkt  = svc->getCharacteristic(kPktUuid);
  NimBLERemoteCharacteristic* ver = svc->getCharacteristic(kVerUuid);
  logger::log("dfu: chars ctrl=%d packet=%d version=%d", s_ctrl != nullptr, s_pkt != nullptr,
              ver != nullptr);
  if (!s_ctrl) return fail(Result::kCharMissing);

  // DFU Version (LE uint16, major<<8|minor): 0x0001 = app with buttonless
  // service (Bluefruit BLEDfu); >= 0x0005 = real bootloader (AdaDFU).
  uint16_t version = 0;
  if (ver) {
    NimBLEAttValue v = ver->readValue();
    if (v.size() >= 2) version = (uint16_t)v.data()[0] | ((uint16_t)v.data()[1] << 8);
    logger::log("dfu: peer DFU version %u.%u (raw 0x%04X)", version >> 8, version & 0xFF, version);
  }

  if (!s_ctrl->subscribe(true, on_ctrl_notify, true)) {
    logger::log("dfu: enabling control point notifications failed");
    return fail(Result::kCharMissing);
  }
  delay(100);  // let the CCCD write settle before the first command

  bool is_app_mode = (ver && version == 0x0001) || !s_pkt;
  if (is_app_mode) {
    logger::log("dfu: peer in app mode, sending buttonless trigger");
    uint8_t enter_bl[2] = {OP_START_DFU, firmware_zip::kTypeApplication};
    ctrl_write(enter_bl, sizeof(enter_bl));  // peer may reset before ACKing
    if (!wait_disconnected(5000)) {
      s_client->disconnect();
      wait_disconnected(2000);
    }
    return Result::kButtonlessTriggered;
  }

  // -------------------- Start DFU + image sizes --------------------
  uint8_t start_cmd[2] = {OP_START_DFU, firmware_zip::kTypeApplication};
  if (!ctrl_write(start_cmd, sizeof(start_cmd))) {
    logger::log("dfu: START_DFU write failed");
    return fail(Result::kDisconnectedEarly);
  }
  // 3 x uint32 LE: SD size, BL size, App size.
  uint8_t sizes[12] = {0};
  put_u32le(sizes + 8, bin.size);
  if (!pkt_write(sizes, sizeof(sizes))) {
    logger::log("dfu: image size write failed");
    return fail(Result::kDisconnectedEarly);
  }
  logger::log("dfu: START_DFU app=%lu B", (unsigned long)bin.size);

  // The bootloader erases the app area before answering; give it time.
  uint8_t st = consume_response(OP_START_DFU, 30000);
  logger::log("dfu: START_DFU status=0x%02x", st);
  if (st != STATUS_SUCCESS) return fail(Result::kRemoteError);

  // -------------------- Init packet (.dat) --------------------
  uint8_t init_start[2]    = {OP_INIT_DFU_PARAMS, 0x00};
  uint8_t init_complete[2] = {OP_INIT_DFU_PARAMS, 0x01};
  if (!ctrl_write(init_start, sizeof(init_start))) {
    logger::log("dfu: INIT_DFU_PARAMS start write failed");
    return fail(Result::kDisconnectedEarly);
  }
  uint8_t chunk[20];
  for (uint32_t off = 0; off < dat.size;) {
    uint32_t want = dat.size - off;
    if (want > sizeof(chunk)) want = sizeof(chunk);
    int n = bundle.zip.read(dat, off, chunk, want);
    if (n != (int)want) {
      logger::log("dfu: dat read short off=%lu n=%d", (unsigned long)off, n);
      return fail(Result::kFsError);
    }
    if (!pkt_write(chunk, n)) {
      logger::log("dfu: dat write failed at off=%lu", (unsigned long)off);
      return fail(Result::kDisconnectedEarly);
    }
    off += n;
  }
  logger::log("dfu: sent init packet (%lu B)", (unsigned long)dat.size);
  delay(50);  // let the write-commands drain before the next write-request

  if (!ctrl_write(init_complete, sizeof(init_complete))) {
    logger::log("dfu: INIT_DFU_PARAMS complete write failed");
    return fail(Result::kDisconnectedEarly);
  }
  st = consume_response(OP_INIT_DFU_PARAMS);
  logger::log("dfu: INIT_DFU_PARAMS status=0x%02x", st);
  if (st != STATUS_SUCCESS) return fail(Result::kRemoteError);

  // -------------------- PRN + Receive Firmware --------------------
  const uint16_t prn = cfg.prn;
  uint8_t prn_cmd[3] = {OP_PKT_RECEIPT_NOTIF_REQ, (uint8_t)(prn & 0xFF), (uint8_t)(prn >> 8)};
  if (!ctrl_write(prn_cmd, sizeof(prn_cmd))) {
    logger::log("dfu: PRN request write failed");
    return fail(Result::kDisconnectedEarly);
  }
  uint8_t recv_cmd[1] = {OP_RECEIVE_FW};
  if (!ctrl_write(recv_cmd, sizeof(recv_cmd))) {
    logger::log("dfu: RECEIVE_FW write failed");
    return fail(Result::kDisconnectedEarly);
  }
  logger::log("dfu: PRN=%u, stream START %lu B...", prn, (unsigned long)bin.size);

  // -------------------- Stream --------------------
  status::phase = status::Phase::kStreaming;
  uint8_t  fw_chunk[244];
  uint32_t sent        = 0;
  uint16_t burst       = 0;
  uint32_t next_log_at = 10;
  uint32_t t_start     = millis();

  while (sent < bin.size) {
    uint32_t want = bin.size - sent;
    if (want > payload) want = payload;
    int n = bundle.zip.read(bin, sent, fw_chunk, want);
    if (n != (int)want) {
      logger::log("dfu: bin read short at %lu n=%d", (unsigned long)sent, n);
      return fail(Result::kFsError);
    }
    if (!pkt_write(fw_chunk, n)) {
      logger::log("dfu: packet write %s at %lu", s_connected ? "stalled" : "lost link",
                  (unsigned long)sent);
      return fail(Result::kDisconnectedEarly);
    }
    sent += n;
    burst++;

    // Every `prn` packets the peer reports its received byte count; block on
    // it so we never outrun the bootloader's flash writes.
    if (prn > 0 && burst >= prn) {
      burst = 0;
      Notif nt;
      if (!wait_notification(5000, &nt)) {
        logger::log("dfu: PRN timeout at %lu", (unsigned long)sent);
        return fail(Result::kTimeout);
      }
      if (nt.len >= 5 && nt.data[0] == OP_PKT_RECEIPT_NOTIF) {
        uint32_t peer = (uint32_t)nt.data[1] | ((uint32_t)nt.data[2] << 8) |
                        ((uint32_t)nt.data[3] << 16) | ((uint32_t)nt.data[4] << 24);
        if (peer != sent) {
          logger::log("dfu: PRN mismatch sent=%lu peer=%lu", (unsigned long)sent,
                      (unsigned long)peer);
          return fail(Result::kRemoteError);
        }
      } else if (nt.len >= 3 && nt.data[0] == OP_RESPONSE_CODE) {
        // Final (or error) RECEIVE_FW response: keep it for consume_response.
        s_pending      = nt;
        s_have_pending = true;
        if (nt.data[2] != STATUS_SUCCESS || sent < bin.size) {
          logger::log("dfu: peer aborted stream at %lu status=0x%02x", (unsigned long)sent,
                      nt.data[2]);
          return fail(Result::kRemoteError);
        }
      } else {
        logger::log("dfu: unexpected notif during stream op=0x%02x len=%u", nt.data[0], nt.len);
      }
    }

    uint32_t pct = (uint32_t)((uint64_t)sent * 100 / bin.size);
    status::pct = (uint8_t)pct;
    if (pct >= next_log_at) {
      logger::log("dfu: progress %lu%% (%lu / %lu B)", (unsigned long)pct, (unsigned long)sent,
                  (unsigned long)bin.size);
      next_log_at = pct + 10;
    }
  }
  status::pct = 100;

  uint32_t elapsed = millis() - t_start;
  logger::log("dfu: stream STOP, %lu ms (%lu B/s)", (unsigned long)elapsed,
              elapsed ? (unsigned long)((uint64_t)bin.size * 1000 / elapsed) : 0UL);
  status::phase = status::Phase::kConnecting;

  // -------------------- Final receive response --------------------
  st = consume_response(OP_RECEIVE_FW, 30000);
  logger::log("dfu: RECEIVE_FW status=0x%02x", st);
  if (st != STATUS_SUCCESS) return fail(Result::kRemoteError);

  // -------------------- Validate --------------------
  uint8_t validate_cmd[1] = {OP_VALIDATE};
  if (!ctrl_write(validate_cmd, sizeof(validate_cmd))) {
    logger::log("dfu: VALIDATE write failed");
    return fail(Result::kDisconnectedEarly);
  }
  st = consume_response(OP_VALIDATE);
  logger::log("dfu: VALIDATE status=0x%02x", st);
  if (st != STATUS_SUCCESS) return fail(Result::kRemoteError);

  // -------------------- Activate and reset --------------------
  // The peer copies/activates the image and then resets, which drops the
  // link. Never disconnect ourselves here: an early disconnect can leave the
  // image half-applied.
  uint8_t activate_cmd[1] = {OP_ACTIVATE_AND_RESET};
  ctrl_write(activate_cmd, sizeof(activate_cmd));
  logger::log("dfu: ACTIVATE sent, waiting for peer reset...");
  if (!wait_disconnected(120000)) {
    logger::log("dfu: peer did not disconnect within 120 s, forcing");
    s_client->disconnect();
    wait_disconnected(3000);
    return Result::kTimeout;
  }
  logger::log("dfu: DONE");
  return Result::kOk;
}

}  // namespace dfu_legacy

# drone_updater

Fly a tiny ESP32 board up to a hard-to-reach **Seeed XIAO nRF52840** and update its firmware over Bluetooth, with no cables and no ladder.

```
laptop ──WiFi──▶ ESP32 Super Mini (on the drone) ──Bluetooth──▶ XIAO nRF52840
  upload zip +        stores the firmware,                       Adafruit bootloader
  config              counts down, then flashes                  ("AdaDFU")
```

Works on an **ESP32-S3 Super Mini** (RGB LED) or an **ESP32-C3 Super Mini** (blue LED). It's the ESP32 successor to [`xiao_nrf52_updater`](https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX/releases/). Instead of showing up as a USB drive, the board hosts a small WiFi web page.

---

## Quick start

**On the bench**

1. Flash the ESP32 once ([For developers](#for-developers)).
2. Power it. Join WiFi **`DroneDFU-XXXX`**, password **`dronedfu`**.
3. Open **http://192.168.4.1/**. Upload your firmware `.zip` ([Making the zip](#making-the-zip)) and paste your config with at least `ble_name` set. Or use curl:
   ```bash
   curl -X POST -H "Content-Type: text/plain" --data-binary @config.txt http://192.168.4.1/config
   curl -F "fw=@firmware.zip" http://192.168.4.1/upload
   ```
4. The LED turns **yellow** (countdown). Unplug it.

**In the field**

5. Power the ESP32 from the drone battery. The countdown starts again from the top (`autostart_delay`, 240 s by default). Get the drone within Bluetooth range of the XIAO before it runs out.
6. The ESP32 finds the XIAO, switches it into its bootloader, and sends the firmware. Watch the LED ([Reading the LED](#reading-the-led)).
7. Land. **Solid green = updated.** Solid red = failed: join the WiFi again and read the log at http://192.168.4.1/log.

> The countdown restarts every time you upload or save the config, so the drone won't take off flashing while you're still editing. Press **Cancel countdown** to stay on the bench, or **Start now** to skip the wait.

---

## Reading the LED

| What you see: S3 (RGB) | What you see: C3 (blue) | Meaning | What to do |
|---|---|---|---|
| 🔵 blue, slow blink (½ s on, ½ s off) | slow blink (½ s on, ½ s off) | No firmware stored | Upload a `.zip` |
| 🔵 blue, short blip every 2 s | short blip every 2 s | Firmware stored, but not counting down (cancelled, or `autostart_delay=0`) | Press **Start now**, save config, or reboot |
| 🟡 yellow, 1 blink per second | double-blink every second | Countdown running: flashing starts when it ends | Fly to the target |
| 🔵 blue, fast blink (2.5× per second) | fast blink (2.5× per second) | Scanning for the XIAO (WiFi is off) | Get closer to the target |
| 🩵 cyan, solid | solid on | Connected and talking to the XIAO, **or** waiting between attempts | Hold position |
| 🟢 green blink, speeding up | blink, speeding up | Sending firmware: slow at 0%, nearly steady near 100% | Hold position. Don't fly away |
| 🟢 green, solid | solid on | **Success**: XIAO updated, stored zip deleted | Land |
| 🔴 red, solid | three quick flashes, then a pause | **Failed** after all retries | Land, read the log ([Troubleshooting](#troubleshooting)) |

On the C3, "solid on" means *connected* while flashing is in progress and *success* once it's over. If you're not sure which, check http://192.168.4.1/status or the log.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Fast blue blink forever, never connects | Name doesn't match, signal too weak, or the XIAO app can't be switched remotely | Check that `ble_name` is part of the app's advertised name. Lower `min_rssi` (e.g. -90) or get closer. Make sure the app includes `BLEDfu` ([Preparing the XIAO](#preparing-the-xiao)). Set `scan_debug=1` and read the log to see what's nearby |
| Cyan, then red after a few minutes | Link kept dropping, or the XIAO's bootloader got stuck mid-update | Get closer. Leave `wedge_cooldown` in place: it gives a stuck bootloader time to reset. Try `prn=4` or lower, or `high_mtu=0` |
| Upload says "Rejected: …" | Not a DFU zip, a compressed zip, or not an application bundle | Rebuild it with `adafruit-nrfutil dfu genpkg` ([Making the zip](#making-the-zip)) |
| Config upload via curl seems ignored | Missing `Content-Type: text/plain` | Use the exact curl line in [Quick start](#quick-start) |
| WiFi network disappeared | Expected: WiFi is off while scanning and flashing | Wait for it to finish, or power-cycle to get WiFi back |
| WiFi name/password change didn't apply | WiFi settings are only read at boot | `curl -X POST http://192.168.4.1/reboot` |
| Garbled or missing serial output | Two programs (e.g. `tio` and `pio device monitor`) have the port open | Close one |

---

## Preparing the XIAO

- **Bootloader:** the Adafruit nRF52 bootloader. Seeed's XIAO nRF52840 ships with a compatible one (`xiao_nrf52840_ble_bootloader-*`).
- **App:** it must include the Bluefruit DFU service (`BLEDfu bledfu; bledfu.begin();` in an Adafruit Bluefruit sketch). That's what lets the ESP32 switch it into the bootloader remotely. **Without it, an over-the-air update is impossible.**
- **Name:** its advertised Bluetooth name must contain your `ble_name`.

## Making the zip

```bash
pip install adafruit-nrfutil
adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application firmware.hex firmware.zip
```

Arduino IDE and PlatformIO builds for Adafruit nRF52 boards already produce a suitable `firmware.zip`. Only **application** updates are supported. SoftDevice and bootloader updates are deliberately not.

---

## Configuration

Plain `key=value` text. Start from [`config.txt`](config.txt) and save it from the web page or with curl. Missing keys use the default.

**You must set**

| Key | Default | What it does |
|---|---|---|
| `ble_name` | *(empty)* | Part of the XIAO app's advertised name, e.g. `XIAO`. The bootloader name `AdaDFU` is always accepted too. |

**Field tuning**

| Key | Default | What it does | Adjust when… |
|---|---|---|---|
| `autostart_delay` | 240 | Seconds from power-on (or last upload/save) until flashing starts. 0 = only start from the web page | You need more or less time to get the drone into position |
| `min_rssi` | -90 | Ignore devices weaker than this (dBm). -127 = no limit | **Raise** (e.g. -75) to refuse flashing on a marginal link. **Lower** if it never finds the target |
| `tx_power` | 9 | ESP32 Bluetooth transmit power, dBm (-24..+20) | Use 20 for maximum range, especially on the C3 |
| `scan_timeout` | 0 | Seconds to look for the XIAO before giving up. 0 = forever | Set it if you'd rather fail (red LED) than search indefinitely |
| `retries` | 3 | Flash attempts before giving up | Raise it for flaky links |

**Advanced**

| Key | Default | What it does |
|---|---|---|
| `prn` | 8 | The XIAO confirms receipt every *n* packets. Lower = gentler and slower, higher = faster and riskier. 0 = no confirmations (not recommended) |
| `high_mtu` | 1 | 1 = big 244-byte packets, about 5× faster. 0 = 20-byte packets, for picky bootloaders |
| `retry_cooldown` | 5 | Seconds to wait after a failed connection attempt |
| `wedge_cooldown` | 60 | Seconds to wait after a failure mid-update, so a stuck bootloader can reset itself |
| `scan_debug` | 0 | 1 = log every nearby device that was skipped, and why. Devices weaker than `min_rssi` are never logged. Leave it off in the field |
| `wifi_ssid` | *(empty)* | WiFi network name. Empty = `DroneDFU-XXXX`. Applies after reboot |
| `wifi_pass` | `dronedfu` | WiFi password. Fewer than 8 characters = open network. Applies after reboot |

---

## Web page & curl

Everything on the web page can also be done with curl, once you've joined the `DroneDFU-XXXX` WiFi:

```bash
# Upload config (the text/plain header is required, or the config is ignored)
curl -X POST -H "Content-Type: text/plain" --data-binary @config.txt http://192.168.4.1/config

# Upload firmware (checked on the board; HTTP 400 + reason if rejected)
curl -F "fw=@firmware.zip" http://192.168.4.1/upload

curl http://192.168.4.1/status             # state, countdown, progress, last result
curl -X POST http://192.168.4.1/start      # start flashing now
curl -X POST http://192.168.4.1/cancel     # cancel the countdown
curl http://192.168.4.1/log                # read the log
curl -X POST http://192.168.4.1/log/clear  # clear the log
curl -X POST http://192.168.4.1/reboot     # reboot (applies WiFi changes)
```

| Route | Purpose |
|---|---|
| `GET /` | the web page |
| `GET /status` | JSON: state, countdown, progress %, last result, stored firmware, free space |
| `POST /upload` | firmware upload; checked, then stored |
| `GET` / `POST /config` | config text (comments kept) |
| `GET /log`, `POST /log/clear` | log (older lines move to `log.old` at 64 KB) |
| `POST /start`, `POST /cancel` | start now / cancel countdown |
| `POST /reboot` | restart the board |

## Serial debug output

Connect USB and run `pio device monitor` (or `tio /dev/tty.usbmodem…`; one program at a time). Every log line also goes to the log file. Key milestones are tagged for easy searching:

```
==== drone_updater boot: ESP32-S3 Super Mini, built ... ====
ble:  START / STARTED                       Bluetooth stack up (address, TX power)
wifi: START / STARTED / STOP / STOPPED      access point up / down
countdown: DFU starts in N s                every 30 s, then each of the last 10 s
==== OTA DFU START (trigger: auto-start|web) ====
ble:  scan START / scan STOP                found vs. timeout, and how long it took
ble:  CONNECT / CONNECTED / DISCONNECTED reason=0x..
dfu:  attempt n/N, stream START / stream STOP (B/s), attempt result
==== OTA DFU STOP: SUCCESS|FAILED after N s (...) ====
```

With no USB host attached (on battery), serial output is simply discarded and never slows the update.

---

## For developers

### Build & flash

```bash
pio test -e native                         # host unit tests (zip, manifest, config)
pio run -e esp32s3_supermini -t upload     # S3 Super Mini
pio run -e esp32c3_supermini -t upload     # C3 Super Mini
```

If upload doesn't start, hold **BOOT** while plugging in. The storage partition (~2 MB) formats itself on first boot. CI builds both `firmware.bin` and a merged `factory.bin` to flash at `0x0`.

### Layout

```
src/
  main.cpp          state machine: countdown → WiFi off → scan/DFU → WiFi on
  dfu_legacy.*      Nordic Legacy DFU client on NimBLE
  ble_scanner.*     scan filter: name | AdaDFU | MAC/MAC+1 | DFU UUID, min_rssi
  web_ui.*          WiFi AP, HTTP routes, embedded page
  storage.*         LittleFS: firmware, config, validation
  zip_reader.*      uncompressed-zip reader (host-testable)
  firmware_zip.*    manifest.json → application .bin/.dat (host-testable)
  config.*          key=value parser (host-testable)
  logger.*          Serial + log file
  led.*, status.*   LED task, shared run status
test/test_native/   unit tests against a real adafruit-nrfutil zip (test/fixtures/)
```

### Design notes & limitations

- **The Bluetooth link is deliberately conservative:**
  - 15–30 ms connection interval
  - 6 s supervision timeout
  - 500 ms check that the link is stable after connecting
  - pause after enabling notifications
  - flow control on receipt confirmations (`prn`)
  - 10 ms backoff when the send queue is full
  - 3 s wait after switching the XIAO into its bootloader

  This trades speed for reliability on a moving drone.
- **C3 Super Mini antenna** is notoriously weak. Use `tx_power=20` and a sensible `min_rssi`. The S3 does better.
- WiFi stays off for the whole update sequence, including an endless scan when `scan_timeout=0`.
- Application updates only. No Secure DFU.
- Log timestamps count from boot (no real-time clock).

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <http://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <http://opensource.org/licenses/MIT>)

at your option.

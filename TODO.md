# TODO

## Before tagging v1.0
- [ ] **End-to-end hardware test.** Flash a real XIAO nRF52840 (Adafruit bootloader + a Bluefruit sketch with `BLEDfu`):
  - first on the S3 Super Mini, then on the C3
  - confirm the sequence: scan → buttonless trigger → `AdaDFU` → stream → validate → activate
  - confirm the XIAO runs the new sketch and the stored zip is deleted
- [ ] **Failure-path test.** Power the XIAO off mid-stream. Expect the wedge cooldown, a retry, the red/fail LED, then the AP back up with a readable log.
- [ ] **Web UI test.** Bad zip rejected with a reason; good zip accepted; config save; Start now / Cancel; log view, download and clear.
- [ ] **Check the first GitHub Actions run** (`.github/workflows/build.yml` has never run on GitHub): native tests, both board builds, the merged `factory.bin`.

## Repo hygiene
- [ ] Put a real name in the `LICENSE-MIT` copyright line (currently "drone_updater contributors").
- [ ] Decide what `config.txt` should contain. It currently holds bench-test values that differ from the documented defaults (`prn=1`, `high_mtu=0`, `wedge_cooldown=30`, `min_rssi=-80`, `autostart_delay=120`). Option: reset it to defaults and keep a personal copy in `config.local.txt`, which is git-ignored.
- [ ] README: warn that the default WiFi password `dronedfu` is public, and tell users to change `wifi_pass`.
- [ ] Pin dependency versions in `platformio.ini` for reproducible builds. Currently resolved:
  - espressif32 6.13.0
  - NimBLE-Arduino 2.5.2
  - ArduinoJson 7.4.3
- [ ] Clean build from scratch (`rm -rf .pio`, then `pio test -e native` and both board builds).

## Nice to have
- [ ] Firmware version string, set from the git tag or describe, shown in the boot log and `/status`.
- [ ] Silence `esp_core_dump_flash: No core dump partition found!` at boot: add a coredump partition, or turn off core dump in sdkconfig.
- [ ] Optional BOOT-button control: e.g. press to cancel or start the countdown.
- [ ] README photo of the drone mount.

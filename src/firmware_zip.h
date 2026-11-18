#pragma once

// Parses an `adafruit-nrfutil dfu genpkg` bundle: reads manifest.json and
// resolves the application .bin + .dat entries. Application-only — this
// updater targets apps on a XIAO nRF52840 running the Adafruit bootloader.

#include <stddef.h>

#include "zip_reader.h"

namespace firmware_zip {

// Legacy DFU START_DFU image-type byte for an application image.
constexpr uint8_t kTypeApplication = 0x04;

struct Parsed {
  zip_reader::Entry bin;  // application image
  zip_reader::Entry dat;  // init packet
};

// Parse the archive behind `zip`. On failure writes a human-readable reason
// to `err` and returns false.
bool parse(zip_reader::Reader& zip, Parsed* out, char* err, size_t err_len);

}  // namespace firmware_zip

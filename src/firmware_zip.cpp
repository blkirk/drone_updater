#include "firmware_zip.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace firmware_zip {

namespace {

constexpr uint32_t kMaxManifest = 2048;

bool resolve(zip_reader::Reader& zip, const char* name, const char* what,
             zip_reader::Entry* out, char* err, size_t err_len) {
  if (!name) {
    snprintf(err, err_len, "manifest application entry missing %s", what);
    return false;
  }
  if (!zip.find(name, out)) {
    snprintf(err, err_len, "%s not in zip", name);
    return false;
  }
  if (out->size == 0) {
    snprintf(err, err_len, "%s is empty", name);
    return false;
  }
  return true;
}

}  // namespace

bool parse(zip_reader::Reader& zip, Parsed* out, char* err, size_t err_len) {
  *out = Parsed{};
  if (err_len) err[0] = '\0';

  zip_reader::Entry manifest;
  if (!zip.find("manifest.json", &manifest)) {
    snprintf(err, err_len, "not a DFU zip (no manifest.json, or zip is compressed)");
    return false;
  }
  if (manifest.size == 0 || manifest.size > kMaxManifest) {
    snprintf(err, err_len, "manifest.json size %lu out of range", (unsigned long)manifest.size);
    return false;
  }

  char buf[kMaxManifest];
  int n = zip.read(manifest, 0, buf, manifest.size);
  if (n != (int)manifest.size) {
    snprintf(err, err_len, "manifest.json read truncated");
    return false;
  }

  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, buf, (size_t)n);
  if (jerr) {
    snprintf(err, err_len, "manifest.json parse: %s", jerr.c_str());
    return false;
  }

  JsonObject m = doc["manifest"].as<JsonObject>();
  if (m.isNull()) {
    snprintf(err, err_len, "manifest.json: top-level 'manifest' missing");
    return false;
  }

  JsonObject app = m["application"].as<JsonObject>();
  if (app.isNull()) {
    if (m["softdevice_bootloader"].is<JsonObject>() || m["bootloader"].is<JsonObject>() ||
        m["softdevice"].is<JsonObject>()) {
      snprintf(err, err_len, "only application bundles are supported");
    } else {
      snprintf(err, err_len, "manifest.json: no application section");
    }
    return false;
  }

  // Copy names out of the JSON doc before find() rewinds the reader (the doc
  // owns its strings, so this is just for clarity of lifetime).
  char bin_name[64] = {0}, dat_name[64] = {0};
  const char* b = app["bin_file"] | (const char*)nullptr;
  const char* d = app["dat_file"] | (const char*)nullptr;
  if (b) snprintf(bin_name, sizeof(bin_name), "%s", b);
  if (d) snprintf(dat_name, sizeof(dat_name), "%s", d);

  if (!resolve(zip, b ? bin_name : nullptr, "bin_file", &out->bin, err, err_len)) return false;
  // The Adafruit bootloader rejects DFU without an init packet.
  if (!resolve(zip, d ? dat_name : nullptr, "dat_file", &out->dat, err, err_len)) return false;
  return true;
}

}  // namespace firmware_zip

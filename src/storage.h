#pragma once

// LittleFS-backed storage: firmware bundle, config and log files.

#include <Arduino.h>
#include <FS.h>

#include "firmware_zip.h"
#include "zip_reader.h"

namespace storage {

constexpr const char* kFwPath      = "/fw.zip";
constexpr const char* kFwTmpPath   = "/fw.tmp";
constexpr const char* kFwNamePath  = "/fw.name";   // original upload filename
constexpr const char* kConfigPath  = "/config.txt";
constexpr const char* kLogPath     = "/log.txt";
constexpr const char* kLogOldPath  = "/log.old";

// Mount LittleFS, formatting it if the mount fails. Returns success.
bool begin();
bool mounted();

// File existence without the core's "does not exist" error log.
bool exists(const char* path);

// Load /config.txt over the defaults into config::current(). Returns true if
// the file existed.
bool load_config();

// Persist raw config text (comments preserved) and re-apply it.
bool save_config(const char* text, size_t len);

// Raw /config.txt contents into `out` (NUL-terminated). False if missing.
bool read_config_text(String* out);

// Firmware bundle presence + size (0 if absent) and stored display name.
bool   fw_present(size_t* size = nullptr);
String fw_name();
void   delete_fw();

// Validate the archive at `path` (manifest + bin + dat present).
bool validate_zip(const char* path, firmware_zip::Parsed* out, char* err, size_t err_len);

// ByteSource over an open fs::File.
struct FileSource : zip_reader::ByteSource {
  fs::File f;
  int read(uint32_t off, void* buf, uint32_t len) override;
};

// Holds the stored bundle open for the duration of a DFU run.
struct Bundle {
  FileSource           src;
  zip_reader::Reader   zip;
  firmware_zip::Parsed parsed;

  bool open(char* err, size_t err_len);
  void close();
};

}  // namespace storage

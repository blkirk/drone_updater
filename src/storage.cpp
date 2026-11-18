#include "storage.h"

#include <LittleFS.h>
#include <sys/stat.h>

#include "config.h"

namespace storage {

static bool s_mounted = false;

// stat() instead of LittleFS.exists()/open(): the Arduino core logs an [E]
// line for every open() of a missing file, which the 1 Hz /status poll
// would turn into a flood.
bool exists(const char* path) {
  if (!s_mounted) return false;
  char full[64];
  snprintf(full, sizeof(full), "/littlefs%s", path);
  struct stat st;
  return stat(full, &st) == 0;
}

bool begin() {
  // formatOnFail=true: a blank chip (first flash) gets a fresh filesystem.
  s_mounted = LittleFS.begin(true);
  return s_mounted;
}

bool mounted() { return s_mounted; }

bool read_config_text(String* out) {
  if (!exists(kConfigPath)) return false;
  File f = LittleFS.open(kConfigPath, FILE_READ);
  if (!f) return false;
  *out = f.readString();
  f.close();
  return true;
}

bool load_config() {
  config::apply_defaults(&config::current());
  String text;
  if (!read_config_text(&text)) return false;
  config::parse(&config::current(), text.c_str());
  return true;
}

bool save_config(const char* text, size_t len) {
  if (!s_mounted) return false;
  File f = LittleFS.open(kConfigPath, FILE_WRITE);
  if (!f) return false;
  size_t w = f.write((const uint8_t*)text, len);
  f.close();
  if (w != len) return false;
  load_config();
  return true;
}

bool fw_present(size_t* size) {
  if (size) *size = 0;
  char full[64];
  snprintf(full, sizeof(full), "/littlefs%s", kFwPath);
  struct stat st;
  if (!s_mounted || stat(full, &st) != 0) return false;
  if (size) *size = (size_t)st.st_size;
  return true;
}

String fw_name() {
  if (!exists(kFwNamePath)) return String(kFwPath + 1);
  File f = LittleFS.open(kFwNamePath, FILE_READ);
  if (!f) return String(kFwPath + 1);
  String s = f.readString();
  f.close();
  s.trim();
  return s;
}

void delete_fw() {
  LittleFS.remove(kFwPath);
  LittleFS.remove(kFwNamePath);
}

int FileSource::read(uint32_t off, void* buf, uint32_t len) {
  if (!f) return -1;
  if (!f.seek(off, SeekSet)) return -1;
  return (int)f.read((uint8_t*)buf, len);
}

bool validate_zip(const char* path, firmware_zip::Parsed* out, char* err, size_t err_len) {
  FileSource src;
  src.f = LittleFS.open(path, FILE_READ);
  if (!src.f) {
    snprintf(err, err_len, "cannot open %s", path);
    return false;
  }
  zip_reader::Reader zip(&src);
  bool ok = firmware_zip::parse(zip, out, err, err_len);
  src.f.close();
  return ok;
}

bool Bundle::open(char* err, size_t err_len) {
  close();
  src.f = LittleFS.open(kFwPath, FILE_READ);
  if (!src.f) {
    snprintf(err, err_len, "no firmware stored");
    return false;
  }
  zip.attach(&src);
  if (!firmware_zip::parse(zip, &parsed, err, err_len)) {
    close();
    return false;
  }
  return true;
}

void Bundle::close() {
  if (src.f) src.f.close();
  zip.attach(nullptr);
}

}  // namespace storage

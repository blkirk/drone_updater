#include "logger.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <stdarg.h>

#include "storage.h"

namespace logger {

namespace {
constexpr size_t     kRotateAt = 64 * 1024;
SemaphoreHandle_t    s_mutex   = nullptr;
bool                 s_file_ok = false;
}  // namespace

void begin() {
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
  s_file_ok = storage::mounted();
}

void log(const char* fmt, ...) {
  char body[192];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  if (n < 0) return;

  uint32_t s = millis() / 1000;
  char line[224];
  int total = snprintf(line, sizeof(line), "[%02lu:%02lu:%02lu] %s\n",
                       (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60),
                       (unsigned long)(s % 60), body);
  if (total < 0) return;
  if (total >= (int)sizeof(line)) total = sizeof(line) - 1;

  if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
  Serial.write((const uint8_t*)line, total);
  if (s_file_ok) {
    File f = LittleFS.open(storage::kLogPath, FILE_APPEND);
    if (f) {
      f.write((const uint8_t*)line, total);
      size_t sz = f.size();
      f.close();
      if (sz > kRotateAt) {
        LittleFS.remove(storage::kLogOldPath);
        LittleFS.rename(storage::kLogPath, storage::kLogOldPath);
      }
    }
  }
  if (s_mutex) xSemaphoreGive(s_mutex);
}

void clear() {
  if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
  LittleFS.remove(storage::kLogPath);
  LittleFS.remove(storage::kLogOldPath);
  if (s_mutex) xSemaphoreGive(s_mutex);
}

}  // namespace logger

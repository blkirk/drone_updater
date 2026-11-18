#pragma once

namespace logger {

// Call once storage is mounted; before that, log() only writes to Serial.
void begin();

// printf-style line → Serial and /log.txt (prefixed with boot-relative
// [hh:mm:ss]). The file rotates to /log.old past 64 KB. Thread-safe.
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Delete /log.txt and /log.old.
void clear();

}  // namespace logger

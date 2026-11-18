#pragma once

// Minimal walker for STORED (uncompressed) ZIP archives — what both Nordic
// `nrfutil` and `adafruit-nrfutil dfu genpkg` produce (Python ZipFile default).
// Walks Local File Headers from the start of the archive and stops at the
// central directory. Pure C++: no Arduino dependency, so it runs in host tests.

#include <stddef.h>
#include <stdint.h>

namespace zip_reader {

// Random-access byte source the archive is read from. Implemented over
// fs::File on the device and over a RAM buffer in host tests.
struct ByteSource {
  virtual ~ByteSource() = default;
  // Read up to `len` bytes at absolute offset `off`. Returns bytes read, or
  // -1 on error. Short reads only happen at end of data.
  virtual int read(uint32_t off, void* buf, uint32_t len) = 0;
};

// Simple RAM-backed source (used by tests; handy for small blobs).
struct MemSource : ByteSource {
  const uint8_t* data;
  uint32_t       size;
  MemSource(const uint8_t* d, uint32_t n) : data(d), size(n) {}
  int read(uint32_t off, void* buf, uint32_t len) override;
};

struct Entry {
  char     name[64];
  uint32_t data_offset;  // absolute byte offset of the entry's data
  uint32_t size;         // size in bytes (STORED: compressed == uncompressed)
};

class Reader {
 public:
  explicit Reader(ByteSource* src = nullptr) : src_(src) {}
  void attach(ByteSource* src) { src_ = src; cursor_ = 0; }

  // Restart iteration at the first entry.
  void rewind() { cursor_ = 0; }

  // Next entry, or false at end / on a malformed or compressed entry.
  bool next(Entry* out);

  // Find an entry by exact (case-sensitive) name. Rewinds first.
  bool find(const char* name, Entry* out);

  // Read up to `len` bytes of entry `e` starting at `offset` within it.
  // Returns bytes read (0 at end), or -1 on error.
  int read(const Entry& e, uint32_t offset, void* buf, uint32_t len);

 private:
  bool read_u16(uint32_t off, uint16_t* v);
  bool read_u32(uint32_t off, uint32_t* v);

  ByteSource* src_    = nullptr;
  uint32_t    cursor_ = 0;  // offset of the next Local File Header
};

}  // namespace zip_reader

#include "zip_reader.h"

#include <string.h>

namespace zip_reader {

namespace {
constexpr uint32_t kLfhSig     = 0x04034b50;  // "PK\x03\x04"
constexpr uint16_t kFlagDataDescriptor = 0x0008;
}  // namespace

int MemSource::read(uint32_t off, void* buf, uint32_t len) {
  if (off > size) return -1;
  if (len > size - off) len = size - off;
  memcpy(buf, data + off, len);
  return (int)len;
}

bool Reader::read_u16(uint32_t off, uint16_t* v) {
  uint8_t b[2];
  if (src_->read(off, b, 2) != 2) return false;
  *v = (uint16_t)(b[0] | (b[1] << 8));
  return true;
}

bool Reader::read_u32(uint32_t off, uint32_t* v) {
  uint8_t b[4];
  if (src_->read(off, b, 4) != 4) return false;
  *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  return true;
}

bool Reader::next(Entry* out) {
  if (!src_) return false;

  // Local File Header:
  //   0 sig(4) 4 version(2) 6 flags(2) 8 method(2) 10 mtime(2) 12 mdate(2)
  //  14 crc32(4) 18 csize(4) 22 usize(4) 26 namelen(2) 28 extralen(2) 30 name
  uint32_t sig;
  if (!read_u32(cursor_, &sig) || sig != kLfhSig) return false;  // CD or EOF

  uint16_t flags, method, namelen, extralen;
  uint32_t csize, usize;
  if (!read_u16(cursor_ + 6, &flags))     return false;
  if (!read_u16(cursor_ + 8, &method))    return false;
  if (!read_u32(cursor_ + 18, &csize))    return false;
  if (!read_u32(cursor_ + 22, &usize))    return false;
  if (!read_u16(cursor_ + 26, &namelen))  return false;
  if (!read_u16(cursor_ + 28, &extralen)) return false;

  // STORED only, and sizes must be in the LFH (no trailing data descriptor).
  if (method != 0 || (flags & kFlagDataDescriptor) || csize != usize) return false;

  uint16_t n = namelen;
  if (n >= sizeof(out->name)) n = sizeof(out->name) - 1;
  if (src_->read(cursor_ + 30, out->name, n) != n) return false;
  out->name[n] = '\0';

  out->data_offset = cursor_ + 30 + namelen + extralen;
  out->size        = usize;
  cursor_          = out->data_offset + csize;
  return true;
}

bool Reader::find(const char* name, Entry* out) {
  rewind();
  Entry e;
  while (next(&e)) {
    if (strcmp(e.name, name) == 0) {
      *out = e;
      return true;
    }
  }
  return false;
}

int Reader::read(const Entry& e, uint32_t offset, void* buf, uint32_t len) {
  if (!src_) return -1;
  if (offset >= e.size) return 0;
  if (len > e.size - offset) len = e.size - offset;
  return src_->read(e.data_offset + offset, buf, len);
}

}  // namespace zip_reader

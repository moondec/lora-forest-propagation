// Little-endian field packing shared by the packet and log-record codecs.
#pragma once

#include <stdint.h>

namespace core {

inline void putU16(uint8_t* p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = v >> 8;
}
inline void putU32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xFF;
}
inline uint16_t getU16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline uint32_t getU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline void putI16(uint8_t* p, int16_t v) { putU16(p, static_cast<uint16_t>(v)); }
inline void putI32(uint8_t* p, int32_t v) { putU32(p, static_cast<uint32_t>(v)); }
inline int16_t getI16(const uint8_t* p) { return static_cast<int16_t>(getU16(p)); }
inline int32_t getI32(const uint8_t* p) { return static_cast<int32_t>(getU32(p)); }

}  // namespace core

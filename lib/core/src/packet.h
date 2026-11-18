// 20-byte over-the-air packet (layout in firmware/DESIGN.md).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace core {

constexpr size_t kPacketLen = 20;
constexpr uint8_t kMagic = 0x4C;  // 'L'
constexpr uint8_t kPacketVersion = 1;

struct Packet {
  uint8_t version;
  uint8_t nodeId;
  uint8_t sf;
  uint32_t cycle;
  uint8_t fix;  // 0 none, 1 time only, 2 position fix
  uint8_t nsat;
  uint16_t hdop100;
  int8_t tempC;
  uint16_t supplyMv;  // 0 = not measured
  uint8_t fwVersion;
  uint16_t seq;
};

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection.
uint16_t crc16Ccitt(const uint8_t* d, size_t n);
// CRC-32 (IEEE, as zlib.crc32): pass the previous result to continue over chunks, 0 to start.
uint32_t crc32Ieee(uint32_t crc, const uint8_t* d, size_t n);

void encodePacket(const Packet& p, uint8_t out[kPacketLen]);
// False on wrong length, magic or application CRC: such frames are not ours.
bool decodePacket(const uint8_t* d, size_t n, Packet* out);
bool matchesSlot(const Packet& p, uint32_t cycle, uint8_t sf);

}  // namespace core

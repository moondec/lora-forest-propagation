#include "packet.h"

#include <string.h>

#include "bytes.h"

namespace core {

uint16_t crc16Ccitt(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= static_cast<uint16_t>(d[i]) << 8;
    for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

uint32_t crc32Ieee(uint32_t crc, const uint8_t* d, size_t n) {
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) {
    crc ^= d[i];
    for (int b = 0; b < 8; ++b) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
  }
  return ~crc;
}

void encodePacket(const Packet& p, uint8_t out[kPacketLen]) {
  memset(out, 0, kPacketLen);
  out[0] = kMagic;
  out[1] = p.version;
  out[2] = p.nodeId;
  out[3] = p.sf;
  putU32(out + 4, p.cycle);
  out[8] = p.fix;
  out[9] = p.nsat;
  putU16(out + 10, p.hdop100);
  out[12] = static_cast<uint8_t>(p.tempC);
  putU16(out + 13, p.supplyMv);
  out[15] = p.fwVersion;
  putU16(out + 16, p.seq);
  putU16(out + 18, crc16Ccitt(out, 18));
}

bool decodePacket(const uint8_t* d, size_t n, Packet* out) {
  if (n != kPacketLen || d[0] != kMagic) return false;
  if (getU16(d + 18) != crc16Ccitt(d, 18)) return false;
  out->version = d[1];
  out->nodeId = d[2];
  out->sf = d[3];
  out->cycle = getU32(d + 4);
  out->fix = d[8];
  out->nsat = d[9];
  out->hdop100 = getU16(d + 10);
  out->tempC = static_cast<int8_t>(d[12]);
  out->supplyMv = getU16(d + 13);
  out->fwVersion = d[15];
  out->seq = getU16(d + 16);
  return true;
}

bool matchesSlot(const Packet& p, uint32_t cycle, uint8_t sf) { return p.cycle == cycle && p.sf == sf; }

}  // namespace core

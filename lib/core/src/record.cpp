#include "record.h"

#include <stdio.h>
#include <string.h>

#include "bytes.h"

namespace core {

void copyText(char* dst, const char* src, size_t cap) {
  size_t i = 0;
  for (; src && src[i] && i + 1 < cap; ++i) dst[i] = src[i];
  dst[i] = '\0';
}

static void putText(uint8_t* p, const char* s, size_t width) {
  memset(p, 0, width);
  for (size_t i = 0; s[i] && i < width; ++i) p[i] = static_cast<uint8_t>(s[i]);
}

static void getText(const uint8_t* p, char* dst, size_t width) {
  memcpy(dst, p, width);
  dst[width] = '\0';
}

void RxSlotRec::encode(uint8_t o[kRecordLen]) const {
  o[0] = kRecRxSlot;
  o[1] = sf;
  putU32(o + 2, cycle);
  o[6] = flags;
  o[7] = preambleOnly;
  putI16(o + 8, rssiPkt10);
  putI16(o + 10, snr100);
  putI16(o + 12, signalRssi10);
  putI16(o + 14, freqErrHz);
  putI32(o + 16, lat1e7);
  putI32(o + 20, lon1e7);
  o[24] = hdop10;
  o[25] = nsat;
  o[26] = static_cast<uint8_t>(tempC);
  o[27] = txFix;
  putU16(o + 28, txSeq);
  o[30] = static_cast<uint8_t>(txTempC);
  o[31] = rxFix;
}

bool RxSlotRec::decode(const uint8_t* d, RxSlotRec* r) {
  if (d[0] != kRecRxSlot) return false;
  r->sf = d[1];
  r->cycle = getU32(d + 2);
  r->flags = d[6];
  r->preambleOnly = d[7];
  r->rssiPkt10 = getI16(d + 8);
  r->snr100 = getI16(d + 10);
  r->signalRssi10 = getI16(d + 12);
  r->freqErrHz = getI16(d + 14);
  r->lat1e7 = getI32(d + 16);
  r->lon1e7 = getI32(d + 20);
  r->hdop10 = d[24];
  r->nsat = d[25];
  r->tempC = static_cast<int8_t>(d[26]);
  r->txFix = d[27];
  r->txSeq = getU16(d + 28);
  r->txTempC = static_cast<int8_t>(d[30]);
  r->rxFix = d[31];
  return true;
}

void RxFrameRec::encode(uint8_t o[kRecordLen]) const {
  memset(o, 0, kRecordLen);
  o[0] = kRecRxFrame;
  o[1] = sf;
  putU32(o + 2, cycle);
  putU32(o + 6, rxCycle);
  o[10] = rxSf;
  o[11] = flags;
  putI32(o + 12, arrivalUs);
  putI16(o + 16, rssiPkt10);
  putI16(o + 18, snr100);
  o[20] = txNode;
  putU16(o + 21, txSeq);
}

bool RxFrameRec::decode(const uint8_t* d, RxFrameRec* r) {
  if (d[0] != kRecRxFrame) return false;
  r->sf = d[1];
  r->cycle = getU32(d + 2);
  r->rxCycle = getU32(d + 6);
  r->rxSf = d[10];
  r->flags = d[11];
  r->arrivalUs = getI32(d + 12);
  r->rssiPkt10 = getI16(d + 16);
  r->snr100 = getI16(d + 18);
  r->txNode = d[20];
  r->txSeq = getU16(d + 21);
  return true;
}

void TxRec::encode(uint8_t o[kRecordLen]) const {
  memset(o, 0, kRecordLen);
  o[0] = kRecTx;
  o[1] = sf;
  putU32(o + 2, cycle);
  o[6] = status;
  o[7] = fix;
  putI32(o + 8, txOffsetUs);
  o[12] = static_cast<uint8_t>(tempC);
  o[13] = nsat;
  putU16(o + 14, hdop100);
  putU16(o + 16, seq);
  putI32(o + 18, lat1e7);
  putI32(o + 22, lon1e7);
}

bool TxRec::decode(const uint8_t* d, TxRec* r) {
  if (d[0] != kRecTx) return false;
  r->sf = d[1];
  r->cycle = getU32(d + 2);
  r->status = d[6];
  r->fix = d[7];
  r->txOffsetUs = getI32(d + 8);
  r->tempC = static_cast<int8_t>(d[12]);
  r->nsat = d[13];
  r->hdop100 = getU16(d + 14);
  r->seq = getU16(d + 16);
  r->lat1e7 = getI32(d + 18);
  r->lon1e7 = getI32(d + 22);
  return true;
}

void MarkerRec::encode(uint8_t o[kRecordLen]) const {
  o[0] = kRecMarker;
  o[1] = 0;
  putU32(o + 2, cycle);
  putU32(o + 6, utcS);
  putText(o + 10, text, 22);
}

bool MarkerRec::decode(const uint8_t* d, MarkerRec* r) {
  if (d[0] != kRecMarker) return false;
  r->cycle = getU32(d + 2);
  r->utcS = getU32(d + 6);
  getText(d + 10, r->text, 22);
  return true;
}

void ContextRec::encode(uint8_t o[kRecordLen]) const {
  o[0] = kRecContext;
  o[1] = mode;
  putU16(o + 2, heightCm);
  putText(o + 4, stopId, 14);
  putText(o + 18, sessionId, 14);
}

bool ContextRec::decode(const uint8_t* d, ContextRec* r) {
  if (d[0] != kRecContext) return false;
  r->mode = d[1];
  r->heightCm = getU16(d + 2);
  getText(d + 4, r->stopId, 14);
  getText(d + 18, r->sessionId, 14);
  return true;
}

void HeaderRec::encode(uint8_t o[kRecordLen]) const {
  o[0] = kRecHeader;
  o[1] = role;
  o[2] = nodeId;
  o[3] = fwVersion;
  putU32(o + 4, freqHz);
  putU16(o + 8, bwHz100);
  o[10] = crDenom;
  o[11] = static_cast<uint8_t>(txPowerDbm);
  putU16(o + 12, syncWord);
  putU16(o + 14, preamble);
  o[16] = cycleS;
  o[17] = sfA;
  o[18] = sfB;
  o[19] = payloadLen;
  putU32(o + 20, utcS);
  putText(o + 24, fwTag, 8);
}

bool HeaderRec::decode(const uint8_t* d, HeaderRec* r) {
  if (d[0] != kRecHeader) return false;
  r->role = d[1];
  r->nodeId = d[2];
  r->fwVersion = d[3];
  r->freqHz = getU32(d + 4);
  r->bwHz100 = getU16(d + 8);
  r->crDenom = d[10];
  r->txPowerDbm = static_cast<int8_t>(d[11]);
  r->syncWord = getU16(d + 12);
  r->preamble = getU16(d + 14);
  r->cycleS = d[16];
  r->sfA = d[17];
  r->sfB = d[18];
  r->payloadLen = d[19];
  r->utcS = getU32(d + 20);
  getText(d + 24, r->fwTag, 8);
  return true;
}

size_t formatRxCsv(const RxSlotRec& r, const ContextRec& c, char* buf, size_t n) {
  const bool rx = r.flags & kFlagReceived;
  int w;
  if (rx)
    w = snprintf(buf, n, "%s,%.1f,%lu,%u,1,%.1f,%.2f,%.1f", c.stopId, c.heightCm / 100.0,
                 static_cast<unsigned long>(r.cycle), r.sf, r.rssiPkt10 / 10.0, r.snr100 / 100.0,
                 r.signalRssi10 / 10.0);
  else
    w = snprintf(buf, n, "%s,%.1f,%lu,%u,0,,,", c.stopId, c.heightCm / 100.0, static_cast<unsigned long>(r.cycle),
                 r.sf);
  return w < 0 ? 0 : static_cast<size_t>(w);
}

}  // namespace core

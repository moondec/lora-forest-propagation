#include "nmea.h"

#include <stdlib.h>
#include <string.h>

namespace core {

int64_t unixFromDate(int y, int mo, int d, int h, int mi, int s) {
  // Days from civil (H. Hinnant), valid for the proleptic Gregorian calendar.
  y -= mo <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + doe - 719468;
  return days * 86400 + h * 3600 + mi * 60 + s;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static int twoDigits(const char* p) { return (p[0] - '0') * 10 + (p[1] - '0'); }

// ddmm.mmmm / dddmm.mmmm with hemisphere -> signed decimal degrees.
static bool parseCoord(const char* v, const char* hemi, int degDigits, double* out) {
  if (strlen(v) < static_cast<size_t>(degDigits + 2) || !hemi[0]) return false;
  char deg[4] = {0};
  memcpy(deg, v, degDigits);
  double val = atof(deg) + atof(v + degDigits) / 60.0;
  if (hemi[0] == 'S' || hemi[0] == 'W') val = -val;
  *out = val;
  return true;
}

bool NmeaParser::feed(char c, int64_t localUs) {
  const int64_t gap = localUs - lastByteUs_;
  lastByteUs_ = localUs;
  if (c == '$') {
    // Only a '$' can start a burst: a late UART callback makes fake gaps inside sentences.
    if (gap > kBurstGapUs) burstStartUs_ = localUs;
    len_ = 0;
    return false;
  }
  if (len_ < 0) return false;
  if (c == '\r' || c == '\n') {
    bool ok = len_ > 0 && finishLine();
    len_ = -1;
    return ok;
  }
  if (len_ >= kMaxLine) {
    len_ = -1;  // overlong: drop
    return false;
  }
  line_[len_++] = c;
  return false;
}

bool NmeaParser::finishLine() {
  line_[len_] = '\0';
  char* star = strrchr(line_, '*');
  if (!star || strlen(star) < 3) return false;
  const int hi = hexVal(star[1]), lo = hexVal(star[2]);
  if (hi < 0 || lo < 0) return false;
  uint8_t cs = 0;
  for (char* p = line_; p < star; ++p) cs ^= static_cast<uint8_t>(*p);
  if (cs != ((hi << 4) | lo)) return false;
  *star = '\0';

  char* fields[24];
  int n = 0;
  char* p = line_;
  fields[n++] = p;
  while (*p && n < 24) {
    if (*p == ',') {
      *p = '\0';
      fields[n++] = p + 1;
    }
    ++p;
  }
  if (strlen(fields[0]) != 5) return false;
  const char* type = fields[0] + 2;  // skip the talker ID (GP, GN, BD, GB, GA, GL)
  if (strcmp(type, "RMC") == 0) return parseRmc(fields, n);
  if (strcmp(type, "GGA") == 0) parseGga(fields, n);
  return false;
}

bool NmeaParser::parseRmc(char** f, int n) {
  // 1 time, 2 status, 3-4 lat, 5-6 lon, 9 date
  if (n < 10 || f[2][0] != 'A' || strlen(f[1]) < 6 || strlen(f[9]) != 6) {
    st_.rmcValid = false;
    return false;
  }
  double lat, lon;
  if (!parseCoord(f[3], f[4], 2, &lat) || !parseCoord(f[5], f[6], 3, &lon)) {
    st_.rmcValid = false;
    return false;
  }
  const int dd = twoDigits(f[9]), mo = twoDigits(f[9] + 2), yy = twoDigits(f[9] + 4);
  st_.utcS = unixFromDate(2000 + yy, mo, dd, twoDigits(f[1]), twoDigits(f[1] + 2), twoDigits(f[1] + 4));
  st_.lat = lat;
  st_.lon = lon;
  st_.rmcValid = true;
  markUs_ = burstStartUs_;
  return true;
}

void NmeaParser::parseGga(char** f, int n) {
  // 6 quality, 7 satellites, 8 HDOP
  if (n < 9) return;
  st_.quality = static_cast<uint8_t>(atoi(f[6]));
  st_.nsat = static_cast<uint8_t>(atoi(f[7]));
  st_.hdop = f[8][0] ? static_cast<float>(atof(f[8])) : 99.9f;
}

}  // namespace core

#include "storage.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

#include "batch.h"

namespace storage {

constexpr int kBufRecords = 64, kFlushAt = 16;
constexpr uint32_t kFlushMs = 60000;
constexpr size_t kMinFreeBytes = 64 * 1024;

static uint8_t buf[kBufRecords][core::kRecordLen];
static int pending = 0;
static uint32_t lastFlush = 0, dropped = 0;
static char path[32] = "";
// File offset where the pending batch starts; lets a retry skip records a short write already stored.
static size_t batchStart = 0;
static bool fullFlag = false;
// LittleFS.usedBytes() walks the whole file system, so the free space is measured once and
// then decremented with a 25 % allowance for metadata and partially filled blocks.
static size_t freeEstimate = 0;

static size_t measureFree() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

bool begin() {
  // The partition is labelled "littlefs" (partitions_16mb.csv); the core's default label is "spiffs".
  if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) return false;
  if (!LittleFS.exists("/log")) LittleFS.mkdir("/log");
  freeEstimate = measureFree();
  return true;
}

bool format() {
  LittleFS.end();
  path[0] = '\0';
  pending = 0;
  fullFlag = false;
  if (!LittleFS.format()) return false;
  return begin();
}

bool openNew(uint32_t* number, const core::HeaderRec& h) {
  if (!flush()) return false;
  // Never append to an existing file (e.g. the NVS counter was reset by a full flash erase).
  char candidate[32];
  for (;; ++*number) {
    snprintf(candidate, sizeof candidate, "/log/L%04lu.bin", static_cast<unsigned long>(*number));
    if (!LittleFS.exists(candidate)) break;
  }
  memcpy(path, candidate, sizeof path);
  batchStart = 0;
  fullFlag = false;
  freeEstimate = measureFree();
  uint8_t rec[core::kRecordLen];
  h.encode(rec);
  return append(rec);
}

bool isOpen() { return path[0] != '\0'; }

bool close() {
  if (!isOpen()) return true;
  if (!flush()) return false;
  path[0] = '\0';
  return true;
}
const char* currentName() { return path[0] ? path + 5 : ""; }  // without "/log/"

bool append(const uint8_t rec[core::kRecordLen]) {
  if (!isOpen()) return false;
  if (fullFlag) {
    ++dropped;
    return false;
  }
  if (pending >= kBufRecords) flush();
  if (pending >= kBufRecords) {  // flash keeps failing: drop instead of overrunning the buffer
    ++dropped;
    return false;
  }
  memcpy(buf[pending++], rec, core::kRecordLen);
  return !fullFlag;  // writing happens in service(), away from radio deadlines
}

bool flush() {
  lastFlush = millis();
  if (pending == 0 || !isOpen()) return true;
  const size_t bytes = static_cast<size_t>(pending) * core::kRecordLen;
  if (freeEstimate < kMinFreeBytes + bytes) {
    freeEstimate = measureFree();  // confirm before stopping
    if (freeEstimate < kMinFreeBytes + bytes) {
      fullFlag = true;  // stop cleanly instead of failing mid-record
      dropped += pending;
      pending = 0;
      return false;
    }
  }
  File f = LittleFS.exists(path) ? LittleFS.open(path, "r+") : LittleFS.open(path, FILE_WRITE);
  if (!f) return false;  // keep records in RAM and retry at the next flush
  // A failed earlier attempt may have stored part of this batch: continue after its last whole
  // record, so nothing is written twice and the 32-byte alignment is kept.
  const int done = core::committedRecords(f.size(), batchStart, pending);
  const size_t at = batchStart + static_cast<size_t>(done) * core::kRecordLen;
  const size_t want = static_cast<size_t>(pending - done) * core::kRecordLen;
  f.seek(at);
  const size_t written = f.write(&buf[done][0], want);
  f.close();
  if (written != want) return false;
  batchStart = at + want;
  pending = 0;
  freeEstimate -= bytes + bytes / 4;
  return true;
}

void service() {
  if (pending >= kFlushAt || millis() - lastFlush >= kFlushMs) flush();
}

bool full() { return fullFlag; }
size_t freeKb() { return freeEstimate / 1024; }
uint32_t droppedRecords() { return dropped; }

void listFiles(void (*emit)(const char* line)) {
  flush();
  File dir = LittleFS.open("/log");
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    char line[48];
    snprintf(line, sizeof line, "%s %lu", f.name(), static_cast<unsigned long>(f.size()));
    emit(line);
  }
}

bool erase(const char* name) {
  flush();
  bool ok = true;
  if (strcmp(name, "all") == 0) {
    File dir = LittleFS.open("/log");
    char names[64][24];
    int n = 0;
    for (File f = dir.openNextFile(); f && n < 64; f = dir.openNextFile())
      if (strcmp(f.name(), currentName()) != 0) snprintf(names[n++], sizeof names[0], "%s", f.name());
    dir.close();
    for (int i = 0; i < n; ++i) {
      char p[32];
      snprintf(p, sizeof p, "/log/%s", names[i]);
      LittleFS.remove(p);
    }
  } else {
    if (strcmp(name, currentName()) == 0) return false;  // never delete the file being written
    char p[32];
    snprintf(p, sizeof p, "/log/%s", name);
    ok = LittleFS.remove(p);
  }
  freeEstimate = measureFree();
  if (freeEstimate >= 2 * kMinFreeBytes) fullFlag = false;
  return ok;
}

}  // namespace storage

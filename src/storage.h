// Binary log on LittleFS: 32-byte records buffered in RAM and appended in batches.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "record.h"

namespace storage {

bool begin();
// Starts a new file /log/L<number>.bin whose first record is the header.
bool openNew(uint32_t number, const core::HeaderRec& h);
bool isOpen();
const char* currentName();
// Queues one record in RAM (writes only if the 64-record buffer is full). False if full, closed or dropped.
bool append(const uint8_t rec[core::kRecordLen]);
void flush();
void service();  // writes when 16 records are pending or every 60 s; call only when no radio deadline is near
bool full();     // less than 64 kB free: logging stopped
size_t freeKb();
uint32_t droppedRecords();  // records lost because flash writes kept failing
// "name size" lines, one per file.
void listFiles(void (*emit)(const char* line));
bool erase(const char* name);  // "all" erases every log file except the open one

}  // namespace storage
